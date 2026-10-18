// Regression tests run the production transport with in-memory USB API doubles.
// Private access is used only for I/O injection; assertions observe public operations.
#import "../Sources/USBTransport.mm"
#include <iostream>
#include <cstring>
#include <atomic>
#include <thread>
#include <cassert>

@interface FakeUSBPipe : NSObject {
@public NSMutableArray<NSData*>* transfers;
  NSMutableArray* completions; NSMutableArray<NSMutableData*>* pendingData; BOOL deferCompletion;
  NSData* input; NSUInteger cursor; IOUSBHostIOSourceDescriptors endpoint;
}
@end
@implementation FakeUSBPipe
- (instancetype)init { if ((self = [super init])) transfers=[NSMutableArray new]; completions=[NSMutableArray new]; pendingData=[NSMutableArray new]; endpoint.bcdUSB=0x0200; endpoint.descriptor.bLength=7; endpoint.descriptor.bDescriptorType=5; endpoint.descriptor.bEndpointAddress=2; endpoint.descriptor.bmAttributes=2; endpoint.descriptor.wMaxPacketSize=512; return self; }
- (const IOUSBHostIOSourceDescriptors*)descriptors { return &endpoint; }
- (BOOL)sendIORequestWithData:(NSMutableData*)data bytesTransferred:(NSUInteger*)count completionTimeout:(NSTimeInterval)timeout error:(NSError**)error {
  (void)timeout; (void)error;
  NSUInteger accepted=std::min(data.length,input.length-cursor);
  if(accepted) memcpy(data.mutableBytes,(const uint8_t*)input.bytes+cursor,accepted);
  cursor+=accepted; *count=accepted; return YES;
}
- (BOOL)enqueueIORequestWithData:(NSMutableData*)data completionTimeout:(NSTimeInterval)timeout error:(NSError**)error completionHandler:(void (^)(IOReturn, NSUInteger))completion {
  (void)timeout; (void)error;
  [transfers addObject:[data copy]];
  if (deferCompletion) { [completions addObject:[completion copy]]; [pendingData addObject:data]; }
  else completion(kIOReturnSuccess,data.length);
  return YES;
}
- (BOOL)abortWithError:(NSError**)error { (void)error; return YES; }
@end

@interface FakeUSBControl : NSObject {
@public uint32_t maximum; BOOL injectStatus; BOOL statusSent;
  std::vector<uint8_t> pending;
}
@end
@implementation FakeUSBControl
- (BOOL)sendDeviceRequest:(IOUSBDeviceRequest)request data:(NSMutableData*)data bytesTransferred:(NSUInteger*)count completionTimeout:(NSTimeInterval)timeout error:(NSError**)error {
  (void)timeout; (void)error;
  using namespace horndis::rndis;
  if (request.bmRequestType == 0x21) {
    const uint8_t* bytes=(const uint8_t*)data.bytes;
    pending.assign(bytes,bytes+data.length); *count=data.length; return YES;
  }
  auto type=load32(pending,0); auto id=load32(pending,8);
  if(injectStatus && !statusSent && type==kQueryMessage) {
    std::vector<uint8_t> indication(20,0); store32(indication,0,7); store32(indication,4,20); store32(indication,8,0x4001000b);
    memcpy(data.mutableBytes,indication.data(),indication.size()); *count=indication.size(); statusSent=YES; return YES;
  }
  std::vector<uint8_t> reply(type == kInitializeMessage ? 52 : type == kQueryMessage ? 30 : 16,0);
  store32(reply,0,type | 0x80000000); store32(reply,4,reply.size()); store32(reply,8,id);
  if (type == kInitializeMessage) { store32(reply,16,1); store32(reply,24,1); store32(reply,32,1); store32(reply,36,maximum); }
  if (type == kQueryMessage) { store32(reply,16,6); store32(reply,20,16); reply[24]=2; reply[29]=1; }
  memcpy(data.mutableBytes,reply.data(),reply.size()); *count=reply.size(); return YES;
}
- (void)destroy {}
@end

static bool shortPacket(size_t maxPacket, size_t frameBytes) {
  horndis::RNDISUSBTransport transport;
  FakeUSBPipe* pipe=[FakeUSBPipe new]; pipe->endpoint.descriptor.wMaxPacketSize=maxPacket; transport.impl_->bulkOut=(IOUSBHostPipe*)pipe;
  std::string error;
  bool ok=transport.writeEthernetFrame(std::vector<uint8_t>(frameBytes,0x42),error);
  NSData* wire=pipe->transfers.firstObject;
  auto declared=horndis::rndis::load32(std::span<const uint8_t>((const uint8_t*)wire.bytes,(size_t)wire.length),4);
  bool pass=ok && wire.length % maxPacket != 0 && declared == frameBytes+44;
  std::cout << (pass ? "PASS " : "FAIL ") << "short-packet endpoint=" << maxPacket << " ethernet=" << frameBytes
            << " wire=" << wire.length << " message_length=" << declared << "\n";
  return pass;
}
static bool limit(uint32_t maximum) {
  horndis::RNDISUSBTransport transport;
  FakeUSBPipe* pipe=[FakeUSBPipe new]; FakeUSBControl* control=[FakeUSBControl new]; control->maximum=maximum;
  transport.impl_->control=(IOUSBHostInterface*)control;
  transport.impl_->bulkOut=(IOUSBHostPipe*)pipe;
  std::vector<uint8_t> mac; std::string error;
  bool initialized=transport.initialize(mac,error);
  bool written= initialized && transport.writeEthernetFrame(std::vector<uint8_t>(1514,0x42),error);
  auto size=pipe->transfers.firstObject.length;
  bool pass=!initialized || !written || size <= maximum;
  std::cout << (pass ? "PASS " : "FAIL ") << "negotiated-limit=" << maximum << " init=" << initialized
            << " send=" << written << " wire=" << size << "\n";
  return pass;
}
static bool receiveAggregate(uint32_t deviceReceiveLimit) {
  using namespace horndis;
  RNDISUSBTransport transport;
  FakeUSBPipe* pipe=[FakeUSBPipe new]; FakeUSBControl* control=[FakeUSBControl new]; control->maximum=deviceReceiveLimit;
  transport.impl_->control=(IOUSBHostInterface*)control; transport.impl_->bulkIn=(IOUSBHostPipe*)pipe;
  std::vector<uint8_t> mac; std::string error;
  if(!transport.initialize(mac,error)) return false;
  auto packet=rndis::wrapEthernetFrame(std::vector<uint8_t>(1514,0x42));
  NSMutableData* aggregate=[NSMutableData new];
  for(int n=0;n<3;++n)[aggregate appendBytes:packet.data() length:packet.size()];
  pipe->input=aggregate; pipe->cursor=0;
  std::vector<uint8_t> frame; bool timedOut=false;
  bool read=transport.readEthernetFrame(frame,timedOut,error);
  bool pass=read && frame.size()==1514;
  std::cout << (pass ? "PASS " : "FAIL ") << "aggregate-rx device_rx_limit=" << deviceReceiveLimit
            << " host_advertised_rx_limit=16384"
            << " transfer=" << aggregate.length << " received_frame=" << frame.size() << " error=" << error << "\n";
  return pass;
}

static bool asynchronousStatus(bool enabled) {
  horndis::RNDISUSBTransport transport;
  FakeUSBControl* control=[FakeUSBControl new]; control->maximum=16384; control->injectStatus=enabled;
  transport.impl_->control=(IOUSBHostInterface*)control;
  std::vector<uint8_t> address; std::string error;
  bool initialized=transport.initialize(address,error);
  bool pass=initialized && address.size()==6;
  std::cout << (pass?"PASS ":"FAIL ") << "async-status enabled=" << enabled << " initialized=" << initialized << " error=" << error << "\n";
  return pass;
}

static bool queueDeadline() {
  using namespace std::chrono;
  horndis::RNDISUSBTransport transport;
  FakeUSBPipe* stalled=[FakeUSBPipe new]; stalled->deferCompletion=YES;
  transport.impl_->bulkOut=(IOUSBHostPipe*)stalled;
  std::string error; bool pass=true;
  for (unsigned n=0;n<8;++n) pass &= transport.writeEthernetFrame(std::vector<uint8_t>(60,0x42),error);
  const auto start=steady_clock::now();
  pass &= !transport.writeEthernetFrame(std::vector<uint8_t>(60,0x42),error);
  const auto elapsed=steady_clock::now()-start;
  pass &= error.find("timed out")!=std::string::npos && elapsed>=seconds(4) && elapsed<seconds(7);
  transport.close();
  for (NSUInteger i=0;i<stalled->completions.count;++i) {
    auto completion=(void (^)(IOReturn, NSUInteger))stalled->completions[i];
    completion(kIOReturnAborted,0);
  }
  [stalled->completions removeAllObjects]; [stalled->pendingData removeAllObjects];
  std::cout << (pass?"PASS ":"FAIL ") << "USB capacity wait has a deadline even without cancellation\n";
  return pass;
}

static bool cancelledQueueAndLateCallbacks() {
  using namespace std::chrono;
  FakeUSBPipe* stalled=[FakeUSBPipe new]; stalled->deferCompletion=YES;
  bool pass=true;
  {
    horndis::RNDISUSBTransport transport;
    transport.impl_->bulkOut=(IOUSBHostPipe*)stalled;
    std::string error;
    for (unsigned n=0;n<8;++n) pass &= transport.writeEthernetFrame(std::vector<uint8_t>(60,0x42),error);
    std::atomic_bool done=false; bool accepted=true;
    std::thread writer([&] { std::string cause; accepted=transport.writeEthernetFrame(std::vector<uint8_t>(60,0x42),cause); done=true; });
    std::this_thread::sleep_for(milliseconds(30));
    pass &= !done.load();
    const auto start=steady_clock::now();
    transport.cancelWrites();
    writer.join();
    pass &= done && !accepted && steady_clock::now()-start < seconds(1);
    // abort deliberately produces no completion: close still must return, and
    // the request buffers must remain alive until the late callbacks run.
    transport.close();
    pass &= steady_clock::now()-start < seconds(2);
    FakeUSBPipe* fresh=[FakeUSBPipe new];
    transport.impl_->bulkOut=(IOUSBHostPipe*)fresh;
    pass &= transport.writeEthernetFrame(std::vector<uint8_t>(60,0x43),error);
  }
  for (NSUInteger i=0;i<stalled->completions.count;++i) {
    auto completion=(void (^)(IOReturn, NSUInteger))stalled->completions[i];
    NSMutableData* data=stalled->pendingData[i];
    pass &= data.length==104 && ((const uint8_t*)data.bytes)[44]==0x42;
    completion(kIOReturnSuccess,data.length);
  }
  [stalled->completions removeAllObjects]; [stalled->pendingData removeAllObjects];
  std::cout << (pass?"PASS ":"FAIL ") << "full queue cancellation, bounded close, and callbacks after destruction\n";
  return pass;
}

int main(int argc,char** argv) { @autoreleasepool {
  std::string mode=argc>1?argv[1]:"all"; int failures=0;
  if(mode=="padding"||mode=="all") { failures+=!shortPacket(512,467); failures+=!shortPacket(512,468); failures+=!shortPacket(64,84); }
  if(mode=="limit"||mode=="all") { failures+=!limit(16384); failures+=!limit(512); }
  if(mode=="status"||mode=="all") { failures+=!asynchronousStatus(false); failures+=!asynchronousStatus(true); }
  if(mode=="rx"||mode=="all") { failures+=!receiveAggregate(16384); failures+=!receiveAggregate(2048); }
  if(mode=="cancel"||mode=="all") { failures+=!cancelledQueueAndLateCallbacks(); failures+=!queueDeadline(); }
  return failures?1:0;
}}
