pragma Singleton

import QtQuick

/// Every string the interface shows, in the language it is in.
///
/// The language is read before each string is answered, which is the whole
/// reason this is a singleton instead of a bare call into `controller`: a
/// binding such as `text: Tr.t("nav_library")` then depends on the language as
/// well as on the string, so switching it redraws every word on screen instead
/// of leaving the old ones behind.
QtObject {
    /// The language in force, from the language system `controller` holds.
    readonly property string language: controller.translator.language

    /// The string `key` names, with `{name}` placeholders filled from
    /// `values`. Anything a locale file leaves out is answered in English.
    function t(key, values) {
        void language
        return controller.translator.text(key, values)
    }

    /// The form of `key` that `count` calls for, out of the plural forms the
    /// locale file defines. `{count}` is filled on top of `values`.
    function n(key, count, values) {
        void language
        return controller.translator.plural(key, count, values)
    }
}
