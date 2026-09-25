#include "core/AppController.h"
#include "core/CoverArtProvider.h"
#include "core/Translator.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSvgRenderer>
#include <QTimer>

#include <cstdio>

namespace {

/// Holds every language against English and prints what is wrong with each.
/// A translation is finished when this says nothing, which is what makes it
/// usable from a script; `--check-i18n` exits non-zero when one is not.
int checkLanguages(const QStringList &wanted)
{
    const QStringList languages = wanted.isEmpty() ? Translator::availableLanguages() : wanted;
    if (languages.isEmpty())
    {
        std::fprintf(stderr, "cadenza: no locale files found\n");
        return 1;
    }

    int incomplete = 0;
    for (const QString &language : languages)
    {
        const QStringList problems = Translator::problems(language);
        if (problems.isEmpty())
        {
            std::printf("%s: ok\n", qUtf8Printable(language));
            continue;
        }

        ++incomplete;
        std::printf("%s:\n", qUtf8Printable(language));
        for (const QString &line : problems)
            std::printf("  %s\n", qUtf8Printable(line));
    }

    return incomplete == 0 ? 0 : 1;
}

/// The window's icon: the same drawing the launcher shows and the sidebar
/// displays, taken from the copy of `packaging/cadenza.svg` the build binds
/// into the binary.
///
/// It is rendered here rather than handed to `QIcon` as a file, because a file
/// needs an SVG plugin to be installed beside the application to be readable
/// at all, and a plugin that is missing would leave the icon silently empty
/// rather than wrong. Every size a window manager or a taskbar asks for is one
/// of the ones drawn below; Qt scales between them if something asks for
/// another.
QIcon applicationIcon()
{
    QSvgRenderer renderer{QStringLiteral(":/icons/cadenza.svg")};

    QIcon icon;
    for (const int side : {16, 24, 32, 48, 64, 128, 256})
    {
        QPixmap pixmap(side, side);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        icon.addPixmap(pixmap);
    }
    return icon;
}

}  // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Cadenza"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Cadenza"));
    QGuiApplication::setApplicationVersion(QStringLiteral(CADENZA_VERSION));
    QGuiApplication::setOrganizationName(QStringLiteral("zxuru"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("zxuru.cl"));

    // The desktop entry this window belongs to, so a compositor matches it with
    // `cadenza.desktop` and draws that entry's icon for it; without the name a
    // Wayland window has nothing to match, and the taskbar shows a blank.
    QGuiApplication::setDesktopFileName(QStringLiteral("cadenza"));
    QGuiApplication::setWindowIcon(applicationIcon());

    // The interface draws every control itself, so Basic is the right base:
    // it carries no platform styling we would only have to override.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Cadenza draws its own window chrome and rounds its own corners, so the
    // surface has to be blended against the desktop instead of being opaque.
    // Has to be set before the engine builds the window.
    QQuickWindow::setDefaultAlphaBuffer(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("A local-files music player."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("folder"),
        QStringLiteral("Music folder to index. Defaults to ~/Music or ~/Música."));

    QCommandLineOption dbOption(
        {QStringLiteral("d"), QStringLiteral("database")},
        QStringLiteral("Library index to use instead of the default."),
        QStringLiteral("path"));
    parser.addOption(dbOption);

    QCommandLineOption scanOnlyOption(
        {QStringLiteral("scan")},
        QStringLiteral("Index the folder and exit, without opening a window."));
    parser.addOption(scanOnlyOption);

    QCommandLineOption checkOption(
        {QStringLiteral("check-i18n")},
        QStringLiteral("Check the locale files against English and exit, without opening "
                       "a window. Exits non-zero when one is incomplete. Name one or "
                       "more languages to check only those."));
    parser.addOption(checkOption);

    parser.process(app);

    // Run before anything that needs a library: this reads JSON and nothing
    // else, and is what a translator runs after editing a file.
    if (parser.isSet(checkOption))
        return checkLanguages(parser.positionalArguments());

    const QStringList positional = parser.positionalArguments();
    const QString root = positional.isEmpty()
        ? AppController::defaultRoot()
        : QDir(positional.first()).absolutePath();

    const QString databasePath = parser.isSet(dbOption)
        ? QDir::current().absoluteFilePath(parser.value(dbOption))
        : AppController::defaultDatabasePath();

    AppController controller(databasePath);

    if (!controller.libraryError().isEmpty()) {
        std::fprintf(stderr, "cadenza: %s\n", qPrintable(controller.libraryError()));
        return 1;
    }

    // `--scan` exists so a library can be built from a script or a service
    // unit without a display server.
    if (parser.isSet(scanOnlyOption)) {
        QObject::connect(&controller, &AppController::scanningChanged, &app, [&] {
            if (!controller.scanning()) {
                std::printf("%s\n", qPrintable(controller.status()));
                QCoreApplication::quit();
            }
        });
        QTimer::singleShot(0, &controller, [&] {
            if (positional.isEmpty())
                controller.startInitialScan();
            else
                controller.scan(root);
        });
        return app.exec();
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("cover"), new CoverArtProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);

    engine.loadFromModule("Cadenza", "Main");

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller, &AppController::shutdown);

    // The window is up before the walk starts, so a large library shows
    // progress instead of a blank screen. A folder on the command line wins
    // over the remembered one.
    QTimer::singleShot(0, &controller, [&] {
        if (positional.isEmpty())
            controller.startInitialScan();
        else
            controller.scan(root);
    });

    return app.exec();
}
