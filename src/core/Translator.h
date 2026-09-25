#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

/// One entry of a locale file: a finished string, or the plural forms a count
/// picks between.
struct LocaleEntry
{
    bool plural = false;
    /// The finished string, when the entry is not a plural.
    QString text;
    /// The form for a count of exactly 1.
    QString one;
    /// The form for every other count.
    QString many;
};

/// A line the interface shows: the key of the string, and the values its
/// placeholders are filled from.
///
/// Messages travel as keys rather than as finished sentences so that changing
/// the language rewrites the ones already on screen, instead of leaving each
/// in whatever language it was built in. `text` carries a message that is
/// already words -- one from SQLite, one from libmpv -- which is shown as it
/// comes, because there is nothing to translate it from.
struct Message
{
    QString key{};
    QString text{};
    QVariantMap values{};
    /// Set when the string has plural forms: the count they are chosen by.
    int count = -1;

    [[nodiscard]] bool isEmpty() const { return key.isEmpty() && text.isEmpty(); }

    bool operator==(const Message &) const = default;
};

/// UI strings, system-language detection, and the locale files they come from.
///
/// Every language is one file, `locales/<code>.json` (ISO 639-1, plus a region
/// only when it differs from the base language: `en.json`, `es.json`,
/// `pt-BR.json`). Dropping a file in is the whole registration step: nothing
/// here or in the interface lists the languages by hand.
///
/// A locale file is a flat JSON object. A key holds either the finished
/// string:
///
///     "search": "Search",
///
/// or, when the text depends on a count, its plural forms:
///
///     "status_results": {
///         "one": "1 result found.",
///         "many": "{count} results found."
///     }
///
/// `text()` renders the first and `plural()` the second, and `{name}`
/// placeholders in either are filled from the values they are called with.
/// Only the forms a language needs have to be defined, and anything missing
/// falls back to English, so a partially translated file is always usable.
///
/// The files are compiled into the binary and read from `locales/` beside the
/// executable first, which is what makes a language droppable: a file put
/// there wins over the copy inside the binary. `cadenza --check-i18n` holds
/// every one of them against English.
class Translator : public QObject
{
    Q_OBJECT

    /// Code of the locale file the interface is drawn in, never empty. Setting
    /// it is what switches the language.
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    /// True while no language has been chosen and the system's is followed.
    Q_PROPERTY(bool followsSystem READ followsSystem NOTIFY languageChanged)
    /// Every language with a locale file, English first, as codes.
    Q_PROPERTY(QStringList languages READ languages NOTIFY languageChanged)

public:
    explicit Translator(QObject *parent = nullptr);

    [[nodiscard]] QString language() const { return m_language; }
    [[nodiscard]] bool followsSystem() const { return m_preference.isEmpty(); }
    [[nodiscard]] QStringList languages() const { return availableLanguages(); }

    void setLanguage(const QString &code);
    /// Drops the choice, so the system's language is followed again.
    Q_INVOKABLE void followSystem();

    /// Name a language calls itself with, for the list of languages to choose
    /// from: read from its own locale file, so the only place a language is
    /// named is the file it lives in. A file that names itself nowhere is
    /// listed under its code.
    [[nodiscard]] Q_INVOKABLE QString languageName(const QString &code) const;

    /// The string `key` holds, with `{name}` placeholders filled from
    /// `values`. Addressed without a count, a plural key reads as its `many`
    /// form.
    [[nodiscard]] Q_INVOKABLE QString text(const QString &key,
                                           const QVariantMap &values = QVariantMap()) const;

    /// The form of `key` that `count` calls for. `{count}` is filled on top of
    /// `values`.
    [[nodiscard]] Q_INVOKABLE QString plural(const QString &key,
                                             int count,
                                             const QVariantMap &values = QVariantMap()) const;

    /// `message` in the language in force.
    [[nodiscard]] QString render(const Message &message) const;

    /// Language a locale file with no translation of its own is served by.
    [[nodiscard]] static QString defaultLanguage();

    /// Language the system is set to, or the default when it has no file.
    [[nodiscard]] static QString systemLanguage();

    /// Codes with a locale file, the default language first.
    [[nodiscard]] static QStringList availableLanguages();

    /// Code of the locale file serving `language`, the default when none does.
    [[nodiscard]] static QString resolve(const QString &language);

    /// Everything wrong with one locale file, as printable lines: keys it is
    /// missing, keys English does not have, a file that cannot be read.
    [[nodiscard]] static QStringList problems(const QString &language);

signals:
    void languageChanged();

private:
    /// Language that was chosen, or an empty code while the system's is
    /// followed.
    QString m_preference;
    /// Resolved code of the language in force: what `m_preference` names, or
    /// what the system is set to.
    QString m_language;
    /// Strings of `m_language`, read once so a lookup costs a hash rather than
    /// a file.
    QHash<QString, LocaleEntry> m_entries;

    /// Reads the strings of `language` into the map above.
    void load(const QString &language);
    void apply(const QString &preference);

    /// Entry `key` names in the language in force, else in English, else
    /// nothing at all.
    [[nodiscard]] const LocaleEntry *entryFor(const QString &key) const;
};
