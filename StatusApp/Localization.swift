// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation

// Default to Simplified Chinese without changing global or per-app preferences.
// An explicit macOS per-app language choice takes precedence.
enum StatusLocalization {
    static func bundle(for resources: Bundle, applicationLanguages: [String]?) -> Bundle {
        let preferences = (applicationLanguages ?? []) + ["zh-Hans"]
        let language = Bundle.preferredLocalizations(from: resources.localizations,
                                                      forPreferences: preferences).first ?? "zh-Hans"
        guard let path = resources.path(forResource: language, ofType: "lproj"),
              let localized = Bundle(path: path) else { return resources }
        return localized
    }
}
