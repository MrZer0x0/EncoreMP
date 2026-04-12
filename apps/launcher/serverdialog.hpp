#ifndef LAUNCHER_SERVERDIALOG_HPP
#define LAUNCHER_SERVERDIALOG_HPP

#include <QWidget>
#include <QByteArray>
#include <QProcess>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QComboBox;
class QCheckBox;
class QTextCodec;

namespace Launcher
{
    class ServerDialog : public QWidget
    {
        Q_OBJECT

    public:
        explicit ServerDialog(QWidget* parent = nullptr);
        ~ServerDialog();

        void startServer();
        bool isRunning() const;

    private slots:
        void processReadyReadStandardOutput();
        void processReadyReadStandardError();
        void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
        void processError(QProcess::ProcessError error);
        void stopServer();
        void refreshDecodedLog();

    private:
        struct ServerConfig
        {
            QString localAddress;
            QString port;
            QString serverHomePath;
            QString configPath;
        };

        void appendRawLog(const QByteArray& data);
        void updateLogView();
        QTextCodec* currentCodec() const;
        ServerConfig readServerConfig() const;
        QString resolveServerExecutable() const;
        QString resolveDisplayAddress(const QString& bindAddress) const;
        QString applicationBasePath() const;
        QString backupDirectoryPath() const;
        QString makeBackupArchivePath() const;
        QString createBackupArchive(QString* errorMessage = nullptr) const;
        void cleanupOldLogsIfNeeded();
        void appendStatusLine(const QString& text);

        QLabel* mAddressLabel;
        QLabel* mPortLabel;
        QLabel* mEncodingLabel;
        QComboBox* mEncodingCombo;
        QCheckBox* mRestartCheckBox;
        QPlainTextEdit* mLogView;
        QPushButton* mStopButton;
        QPushButton* mCloseButton;
        QProcess* mProcess;
        QByteArray mRawLog;
        int mRestartCounter;
        bool mStopRequested;
        int mRapidCrashCount;
        qint64 mLastStartMs;
    };
}

#endif
