#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QFileOpenEvent>
#include <QPointer>

namespace {

/// Receives files opened from Finder or the dock on macOS.
class Application : public QApplication {
public:
    using QApplication::QApplication;
    QPointer<MainWindow> window;
    QStringList pending;

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const QString path = static_cast<QFileOpenEvent *>(event)->file();
            if (window) window->openDocuments({path});
            else pending.append(path);
            return true;
        }
        return QApplication::event(event);
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication::setOrganizationName(QStringLiteral("Garimpo"));
    QApplication::setOrganizationDomain(QStringLiteral("lucasdutra.com"));
    QApplication::setApplicationName(QStringLiteral("Garimpo"));
    QApplication::setApplicationVersion(QStringLiteral(GARIMPO_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("garimpo"));

    Application app(argc, argv);
    Theme::apply(app);
    MainWindow window;
    app.window = &window;
    window.show();

    QStringList files = app.arguments().mid(1);
    files.append(app.pending);
    if (!files.isEmpty()) window.openDocuments(files);
    return app.exec();
}
