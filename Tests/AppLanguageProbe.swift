import Foundation

@main
private enum AppLanguageProbe {
    static func main() {
        precondition(CommandLine.arguments.count == 3,
                     "usage: app-language-probe <app-bundle> <status-source>")
        guard let bundle = Bundle(path: CommandLine.arguments[1]) else {
            fatalError("Cannot open app bundle")
        }

        let expectedLocalizations = [
            "en", "zh-Hans", "zh-Hant", "ja", "ko", "fr", "de", "es", "pt-BR", "it", "ru",
        ]
        precondition(bundle.developmentLocalization == "zh-Hans", "Development language must be Chinese")
        for (preferences, language) in [
            (nil, "zh-Hans"), ([], "zh-Hans"), (["xx"], "zh-Hans"),
            (["en"], "en"), (["en-GB"], "en"), (["zh-Hant"], "zh-Hant"), (["ja"], "ja"),
        ] as [([String]?, String)] {
            let selected = StatusLocalization.bundle(for: bundle, applicationLanguages: preferences)
            guard let expectedPath = bundle.path(forResource: language, ofType: "lproj"),
                  let expected = Bundle(path: expectedPath) else { fatalError("Missing language bundle") }
            let expectedName = language == "zh-Hans" ? "有线热点4macOS"
                : language == "zh-Hant" ? "有線熱點4macOS" : "Wired Hotspot 4 macOS"
            precondition(selected.localizedString(forKey: "CFBundleDisplayName", value: "MISSING", table: "InfoPlist") == expectedName,
                         "Localized product name has stale branding or incorrect macOS capitalization")
            let key = "menu.quit"
            precondition(selected.localizedString(forKey: key, value: "MISSING", table: "Localizable") ==
                         expected.localizedString(forKey: key, value: "MISSING", table: "Localizable"),
                         "Default language or explicit app language was ignored")
        }
        print("Chinese default and explicit per-app language overrides passed")
        let referenceKeys = localizationKeys(in: bundle, localization: "en")

        print("localizations=\(bundle.localizations)")
        precondition(Set(bundle.localizations) == Set(expectedLocalizations),
                     "Unexpected app localization set")
        precondition(bundle.localizations.count == expectedLocalizations.count,
                     "Duplicate app localizations")
        for localization in expectedLocalizations {
            precondition(bundle.localizations.contains(localization),
                         "Missing bundle localization: \(localization)")
            precondition(bundle.path(forResource: "InfoPlist",
                                     ofType: "strings",
                                     inDirectory: nil,
                                     forLocalization: localization) != nil,
                         "Missing InfoPlist.strings for \(localization)")
            let keys = localizationKeys(in: bundle, localization: localization)
            precondition(keys == referenceKeys,
                         "Localizable.strings keys differ for \(localization)")
        }
        precondition(referenceKeys.count == 38,
                     "Unexpected localized string count: \(referenceKeys.count)")
        let sourceKeys = localizationKeys(inSource: CommandLine.arguments[2])
        precondition(sourceKeys == referenceKeys,
                     "Swift localization keys differ from Localizable.strings")
    }

    private static func localizationKeys(in bundle: Bundle,
                                         localization: String) -> Set<String> {
        guard let path = bundle.path(forResource: "Localizable",
                                     ofType: "strings",
                                     inDirectory: nil,
                                     forLocalization: localization),
              let dictionary = NSDictionary(contentsOfFile: path) as? [String: String]
        else {
            fatalError("Missing or invalid Localizable.strings for \(localization)")
        }
        precondition(dictionary.values.allSatisfy { !$0.isEmpty },
                     "Empty localization value in \(localization)")
        return Set(dictionary.keys)
    }

    private static func localizationKeys(inSource path: String) -> Set<String> {
        guard let source = try? String(contentsOfFile: path, encoding: .utf8),
              let expression = try? NSRegularExpression(
                pattern: #"localized\(\s*"([A-Za-z0-9.]+)""#
              )
        else {
            fatalError("Cannot read localization keys from status source")
        }
        let range = NSRange(source.startIndex..<source.endIndex, in: source)
        return Set(expression.matches(in: source, range: range).compactMap { match in
            guard let matchRange = Range(match.range(at: 1), in: source) else { return nil }
            return String(source[matchRange])
        })
    }
}
