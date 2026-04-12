#ifndef PLAYPAGE_H
#define PLAYPAGE_H

#include "ui_playpage.h"

#include <QString>

class QWidget;

namespace Launcher
{
    class PlayPage : public QWidget, private Ui::PlayPage
    {
        Q_OBJECT

    public:
        explicit PlayPage(QWidget *parent = nullptr);

        void setServerAddress(const QString& addr);
        void setServerPort(const QString& port);
        void setServerConsoleWidget(QWidget* widget);

        QString serverAddress() const;
        QString serverPort() const;

       void switchToServerConsoleTab();
       void loadServerSettings();
       bool saveServerSettings();

    signals:
        void playButtonClicked();
        void serverButtonClicked();

    private slots:
        void slotPlayClicked();
        void slotServerClicked();
        void slotReloadServerSettings();
        void slotSaveServerSettings();
        void slotApplyFormToRawConfig();
        void slotSyncFormFromRawConfig();

    private:
        QString serverConfigPath() const;
        QString replaceRawValue(const QString& text, const QString& key, const QString& value) const;
        QString updatedConfigFromForm(const QString& input) const;
        void populateFormFromConfig(const QString& text);
        void setServerSettingsStatus(const QString& text, bool isError = false);
        QWidget* mEmbeddedServerConsole;
    };
}
#endif
