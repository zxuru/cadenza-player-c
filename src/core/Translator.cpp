#include "Translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLocale>
#include <QSettings>

#include <optional>
#include <utility>

namespace {

/// Setting the chosen language is remembered in. An absent or empty value
/// means "follow the system", which is also what a first run does.
const char kLanguageKey[] = "ui/language";

const char kExtension[] = ".json";

/// Names a locale file is not: `_notes.json` is a working copy, `.es.json` is
/// an editor's backup.
bool isLanguageFile(const QString &name)
{
    return !name.startsWith(QLatin1Char('_')) && !name.startsWith(QLatin1Char('.'));
}

void warn(const QString &message)
{
    qWarning("i18n: %s", qUtf8Printable(message));
}

/// Directories a language can be dropped into: beside the executable, which is
/// where a build tree and an unpacked archive keep it, and the `share` folder
/// an installed one puts it in.
QStringList diskDirectories()
{
    const QString base = QCoreApplication::applicationDirPath();
    return {base + QStringLiteral("/locales"),
            base + QStringLiteral("/../share/cadenza/locales")};
}

/// Where a language could be, on disk first and inside the binary last.
QStringList candidates(const QString &code)
{
    QStringList paths;
    for (const QString &directory : diskDirectories())
        paths.append(directory + QLatin1Char('/') + code + QLatin1String(kExtension));
    paths.append(QStringLiteral(":/locales/") + code + QLatin1String(kExtension));
    return paths;
}

/// Codes the locale files define, in the directories given.
QStringList codesIn(const QStringList &directories)
{
    QStringList codes;
    for (const QString &directory : directories)
    {
        const QDir dir(directory);
        const QStringList names =
            dir.entryList({QStringLiteral("*") + QLatin1String(kExtension)}, QDir::Files);
        for (const QString &name : names)
        {
            if (isLanguageFile(name))
                codes.append(QFileInfo(name).completeBaseName());
        }
    }
    return codes;
}

/// Comparable form of a language code, `LANG`-style values included:
/// "es_ES.UTF-8", "es-AR" and "es:en" all mean Spanish.
QString normalise(const QString &code)
{
    QString value = code.trimmed().section(QLatin1Char(':'), 0, 0);
    value = value.section(QLatin1Char('.'), 0, 0);
    return value.replace(QLatin1Char('_'), QLatin1Char('-')).toLower();
}

/// "es-ES" -> "es": the language a regional variant also answers to.
QString primary(const QString &code)
{
    return normalise(code).section(QLatin1Char('-'), 0, 0);
}

/// Code of the locale file serving `code`, or an empty string when none does.
/// A regional variant falls back to its base language: es-AR is served by
/// es.json, and pt by pt-BR.json.
QString match(const QString &code, const QStringList &available)
{
    const QString wanted = normalise(code);
    if (wanted.isEmpty())
        return QString();

    for (const QString &candidate : available)
    {
        if (normalise(candidate) == wanted)
            return candidate;
    }
    for (const QString &candidate : available)
    {
        if (primary(candidate) == primary(wanted))
            return candidate;
    }
    return QString();
}

/// One key of a locale file, kept when it is well formed and dropped with a
/// complaint when it is not: the rest of a hand-written file is still worth
/// having.
std::optional<LocaleEntry> readEntry(const QString &key,
                                     const QJsonValue &value,
                                     const QString &path)
{
    if (value.isString())
    {
        LocaleEntry entry;
        entry.text = value.toString();
        return entry;
    }

    if (value.isObject())
    {
        const QJsonObject object = value.toObject();
        LocaleEntry entry;
        entry.plural = !object.isEmpty();

        for (auto it = object.constBegin(); entry.plural && it != object.constEnd(); ++it)
        {
            if (it.key() == QLatin1String("one") && it.value().isString())
                entry.one = it.value().toString();
            else if (it.key() == QLatin1String("many") && it.value().isString())
                entry.many = it.value().toString();
            else
                entry.plural = false;
        }

        if (entry.plural)
            return entry;
    }

    warn(QStringLiteral("%1: \"%2\" must be a string, or an object with \"one\" and "
                        "\"many\" strings")
             .arg(path, key));
    return std::nullopt;
}

/// Reads and validates one locale file. An unreadable one yields no strings at
/// all, which leaves every key to be answered by English.
QHash<QString, LocaleEntry> readCatalog(const QString &code, QString *error)
{
    for (const QString &path : candidates(code))
    {
        QFile file(path);
        if (!file.exists())
            continue;

        if (!file.open(QIODevice::ReadOnly))
        {
            *error = QStringLiteral("%1 cannot be read: %2").arg(path, file.errorString());
            break;
        }

        QJsonParseError failure{};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &failure);
        if (failure.error != QJsonParseError::NoError)
        {
            *error = QStringLiteral("%1 is not valid JSON (%2)").arg(path, failure.errorString());
            break;
        }
        if (!document.isObject())
        {
            *error = QStringLiteral("%1 must hold a JSON object mapping keys to strings").arg(path);
            break;
        }

        QHash<QString, LocaleEntry> entries;
        const QJsonObject object = document.object();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        {
            if (const std::optional<LocaleEntry> entry = readEntry(it.key(), it.value(), path))
                entries.insert(it.key(), *entry);
        }
        return entries;
    }

    if (error->isEmpty())
        *error = QStringLiteral("locales/%1%2 not found").arg(code, QLatin1String(kExtension));
    return {};
}

/// English, read once: nearly every lookup a locale file leaves out is
/// answered by it.
const QHash<QString, LocaleEntry> &englishEntries()
{
    static const QHash<QString, LocaleEntry> entries = [] {
        QString error;
        QHash<QString, LocaleEntry> catalog = readCatalog(Translator::defaultLanguage(), &error);
        if (!error.isEmpty())
            warn(error);
        return catalog;
    }();
    return entries;
}

/// Replaces `{name}` with the value of `name`. A placeholder with no value is
/// left standing: a locale file asking for something the call does not pass is
/// a bug worth seeing, and blanking it would hide the shape of the sentence.
QString fill(const QString &text, const QVariantMap &values)
{
    if (values.isEmpty() || !text.contains(QLatin1Char('{')))
        return text;

    QString filled;
    filled.reserve(text.size());

    int at = 0;
    while (true)
    {
        const int open = text.indexOf(QLatin1Char('{'), at);
        if (open < 0)
            break;

        const int close = text.indexOf(QLatin1Char('}'), open + 1);
        if (close < 0)
            break;

        const QString name = text.mid(open + 1, close - open - 1);
        const auto value = values.constFind(name);
        if (value == values.constEnd())
        {
            filled += text.mid(at, close - at + 1);
        }
        else
        {
            filled += text.mid(at, open - at);
            filled += value->toString();
        }

        at = close + 1;
    }

    filled += text.mid(at);
    return filled;
}

/// The form of `entry` a count of `count` calls for, falling back to the other
/// form when only one was written -- a partially translated file is meant to
/// stay usable -- and reading the whole string when the entry is not a plural
/// at all, which is how a translation spells `{count}` out itself.
QString form(const LocaleEntry &entry, int count, const QVariantMap &values)
{
    if (!entry.plural)
        return fill(entry.text, values);

    const QString wanted = count == 1 ? entry.one : entry.many;
    const QString other = count == 1 ? entry.many : entry.one;
    return fill(wanted.isEmpty() ? other : wanted, values);
}

}  // namespace

Translator::Translator(QObject *parent)
    : QObject(parent)
{
    const QString saved = QSettings().value(QLatin1String(kLanguageKey)).toString();

    // A language that has since lost its file, or a code that was never one,
    // is dropped rather than remembered: following the system beats asking for
    // strings that are not there.
    m_preference = saved.isEmpty() ? QString() : match(saved, availableLanguages());
    if (!saved.isEmpty() && m_preference.isEmpty())
        warn(QStringLiteral("no locale file for \"%1\"; following the system instead").arg(saved));

    m_language = m_preference.isEmpty() ? systemLanguage() : m_preference;
    load(m_language);
}

void Translator::setLanguage(const QString &code)
{
    if (code.isEmpty())
    {
        followSystem();
        return;
    }

    const QString resolved = match(code, availableLanguages());
    if (resolved.isEmpty())
    {
        warn(QStringLiteral("no locale file for \"%1\": the language stays as it is").arg(code));
        return;
    }

    apply(resolved);
}

void Translator::followSystem()
{
    apply(QString());
}

void Translator::apply(const QString &preference)
{
    const QString language = preference.isEmpty() ? systemLanguage() : preference;
    if (preference == m_preference && language == m_language)
        return;

    m_preference = preference;
    m_language = language;
    load(language);

    QSettings().setValue(QLatin1String(kLanguageKey), preference);
    emit languageChanged();
}

void Translator::load(const QString &language)
{
    QString error;
    QHash<QString, LocaleEntry> entries = readCatalog(language, &error);
    if (!error.isEmpty() && language != defaultLanguage())
        warn(error + QStringLiteral("; English stands in for what it holds"));

    m_entries = std::move(entries);
}

const LocaleEntry *Translator::entryFor(const QString &key) const
{
    const auto mine = m_entries.constFind(key);
    if (mine != m_entries.constEnd())
        return &*mine;

    const auto english = englishEntries().constFind(key);
    return english == englishEntries().constEnd() ? nullptr : &*english;
}

QString Translator::text(const QString &key, const QVariantMap &values) const
{
    const LocaleEntry *entry = entryFor(key);
    if (entry == nullptr)
        return key;  // a key nothing defines reads as itself

    // Addressed without a count, a plural key reads as its many form.
    if (!entry->plural)
        return fill(entry->text, values);
    return fill(entry->many.isEmpty() ? entry->one : entry->many, values);
}

QString Translator::plural(const QString &key, int count, const QVariantMap &values) const
{
    QVariantMap withCount = values;
    withCount.insert(QStringLiteral("count"), count);

    const LocaleEntry *entry = entryFor(key);
    if (entry == nullptr)
        return fill(key, withCount);

    return form(*entry, count, withCount);
}

QString Translator::render(const Message &message) const
{
    if (message.key.isEmpty())
        return message.text;

    return message.count >= 0 ? plural(message.key, message.count, message.values)
                              : text(message.key, message.values);
}

QString Translator::defaultLanguage()
{
    return QStringLiteral("en");
}

QStringList Translator::availableLanguages()
{
    QStringList codes = codesIn(diskDirectories());
    for (const QString &code : codesIn({QStringLiteral(":/locales")}))
    {
        if (!codes.contains(code))
            codes.append(code);
    }

    codes.removeDuplicates();
    codes.sort(Qt::CaseInsensitive);

    // English first: it is what every other file is measured against, and what
    // the interface falls back to.
    if (const qsizetype english = codes.indexOf(defaultLanguage()); english > 0)
        codes.move(english, 0);
    return codes;
}

QString Translator::systemLanguage()
{
    // Qt answers from the platform itself: `LANG`/`LC_ALL` on Linux, the user's
    // UI language on Windows, the phone's setting on Android. The variants it
    // lists are tried in turn, so a region with no file lands on its base
    // language.
    const QStringList available = availableLanguages();
    for (const QString &variant : QLocale::system().uiLanguages())
    {
        const QString code = match(variant, available);
        if (!code.isEmpty())
            return code;
    }
    return defaultLanguage();
}

QString Translator::resolve(const QString &language)
{
    const QString code = match(language, availableLanguages());
    return code.isEmpty() ? defaultLanguage() : code;
}

QString Translator::languageName(const QString &code) const
{
    // A picker asks for every language at once and the answer only changes
    // when a file does, so names are read once each.
    static QHash<QString, QString> names;

    const QString key = resolve(code);
    if (const auto known = names.constFind(key); known != names.constEnd())
        return *known;

    QString error;
    const QHash<QString, LocaleEntry> entries = readCatalog(key, &error);
    const auto entry = entries.constFind(QStringLiteral("language_name"));

    // A file that names itself nowhere leaves its code, which still says which
    // language the row is.
    const QString name = entry == entries.constEnd() ? key : entry->text;
    names.insert(key, name);
    return name;
}

QStringList Translator::problems(const QString &language)
{
    // The code is matched first, so checking `es_AR` checks the file that
    // would actually serve it.
    const QString code = match(language, availableLanguages());
    if (code.isEmpty())
        return {QStringLiteral("locales/%1%2 not found").arg(language, QLatin1String(kExtension))};

    QStringList lines;
    QString error;
    const QHash<QString, LocaleEntry> entries = readCatalog(code, &error);
    if (!error.isEmpty())
        lines.append(error);

    QStringList missing;
    for (auto it = englishEntries().constBegin(); it != englishEntries().constEnd(); ++it)
    {
        if (!entries.contains(it.key()))
            missing.append(it.key());
    }

    QStringList unknown;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
    {
        if (!englishEntries().contains(it.key()))
            unknown.append(it.key());
    }

    missing.sort();
    unknown.sort();

    if (!missing.isEmpty())
        lines.append(QStringLiteral("missing %1: %2").arg(missing.size()).arg(missing.join(", ")));
    if (!unknown.isEmpty())
        lines.append(QStringLiteral("unknown %1: %2").arg(unknown.size()).arg(unknown.join(", ")));

    return lines;
}
