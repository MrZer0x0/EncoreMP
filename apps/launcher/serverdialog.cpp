#include "serverdialog.hpp"

#include <QAbstractSocket>
#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTextCodec>
#include <QTextCursor>
#include <QTextStream>
#include <QVBoxLayout>

Launcher::ServerDialog::ServerDialog(QWidget* parent)
    : QWidget(parent)
    , mAddressLabel(nullptr)
    , mPortLabel(nullptr)
    , mEncodingLabel(nullptr)
    , mEncodingCombo(nullptr)
    , mRestartCheckBox(nullptr)
    , mLogView(nullptr)
    , mStopButton(nullptr)
    , mCloseButton(nullptr)
    , mProcess(new QProcess(this))
    , mRestartCounter(0)
    , mStopRequested(false)
    , mRapidCrashCount(0)
    , mLastStartMs(0)
{
    setWindowTitle(tr("TES3MP Server"));
    resize(860, 620);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QHBoxLayout* infoLayout = new QHBoxLayout();
    mAddressLabel = new QLabel(this);
    mPortLabel = new QLabel(this);
    infoLayout->addWidget(mAddressLabel, 1);
    infoLayout->addWidget(mPortLabel, 0);
    mainLayout->addLayout(infoLayout);

    QHBoxLayout* optionsLayout = new QHBoxLayout();
    mEncodingLabel = new QLabel(tr("Log encoding:"), this);
    mEncodingCombo = new QComboBox(this);
    mEncodingCombo->addItem(tr("UTF-8"), QStringLiteral("UTF-8"));
    mEncodingCombo->addItem(tr("System"), QStringLiteral("System"));
    mEncodingCombo->addItem(tr("Windows-1251"), QStringLiteral("Windows-1251"));
    mEncodingCombo->addItem(tr("CP866"), QStringLiteral("CP866"));
    mRestartCheckBox = new QCheckBox(tr("Auto restart and backup"), this);
    mRestartCheckBox->setChecked(true);

    optionsLayout->addWidget(mEncodingLabel);
    optionsLayout->addWidget(mEncodingCombo);
    optionsLayout->addSpacing(16);
    optionsLayout->addWidget(mRestartCheckBox);
    optionsLayout->addStretch(1);
    mainLayout->addLayout(optionsLayout);

    mLogView = new QPlainTextEdit(this);
    mLogView->setReadOnly(true);
    mLogView->setLineWrapMode(QPlainTextEdit::NoWrap);
    mainLayout->addWidget(mLogView, 1);

    QDialogButtonBox* buttons = new QDialogButtonBox(this);
    mStopButton = buttons->addButton(tr("Stop Server"), QDialogButtonBox::ActionRole);
    mCloseButton = buttons->addButton(tr("Clear Log"), QDialogButtonBox::ResetRole);
    mainLayout->addWidget(buttons);

    connect(mStopButton, SIGNAL(clicked()), this, SLOT(stopServer()));
    connect(mCloseButton, SIGNAL(clicked()), mLogView, SLOT(clear()));
    connect(mEncodingCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(refreshDecodedLog()));
    connect(mProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(processReadyReadStandardOutput()));
    connect(mProcess, SIGNAL(readyReadStandardError()), this, SLOT(processReadyReadStandardError()));
    connect(mProcess, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(processFinished(int,QProcess::ExitStatus)));
    connect(mProcess, SIGNAL(error(QProcess::ProcessError)), this, SLOT(processError(QProcess::ProcessError)));
}

Launcher::ServerDialog::~ServerDialog()
{
}

void Launcher::ServerDialog::startServer()
{
    if (mProcess->state() != QProcess::NotRunning)
    {
        return;
    }

    mStopRequested = false;
    mCloseButton->setEnabled(true);
    mStopButton->setEnabled(true);
    if (!mRawLog.isEmpty())
        appendStatusLine(QStringLiteral("----------------------------------------"));

    const ServerConfig config = readServerConfig();
    mAddressLabel->setText(tr("Connect IP: %1").arg(resolveDisplayAddress(config.localAddress)));
    mPortLabel->setText(tr("Port: %1").arg(config.port));

    appendStatusLine(tr("Starting tes3mp-server..."));
    appendStatusLine(tr("Server config: %1").arg(QDir::toNativeSeparators(config.configPath)));
    appendStatusLine(tr("Server data: %1").arg(QDir::toNativeSeparators(config.serverHomePath)));
    appendStatusLine(tr("Log encoding: %1").arg(mEncodingCombo->currentText()));

    const QString executable = resolveServerExecutable();
    if (executable.isEmpty())
    {
        QMessageBox::warning(this, tr("Error starting executable"),
            tr("Could not find tes3mp-server executable next to the launcher."));
        mStopButton->setEnabled(false);
        return;
    }

    mProcess->setProgram(QDir::toNativeSeparators(executable));
    mProcess->setArguments(QStringList());
    mProcess->setProcessChannelMode(QProcess::SeparateChannels);

    const QString workingDirectory = QFileInfo(config.configPath).absolutePath();
    mProcess->setWorkingDirectory(workingDirectory);

    mLastStartMs = QDateTime::currentMSecsSinceEpoch();
    appendStatusLine(tr("Working directory: %1").arg(QDir::toNativeSeparators(workingDirectory)));

    mProcess->start();

    if (!mProcess->waitForStarted(3000))
    {
        QMessageBox::critical(this, tr("Error starting executable"), mProcess->errorString());
        mStopButton->setEnabled(false);
        return;
    }

}



bool Launcher::ServerDialog::isRunning() const
{
    return mProcess != nullptr && mProcess->state() != QProcess::NotRunning;
}

void Launcher::ServerDialog::processReadyReadStandardOutput()
{
    appendRawLog(mProcess->readAllStandardOutput());
}

void Launcher::ServerDialog::processReadyReadStandardError()
{
    appendRawLog(mProcess->readAllStandardError());
}

void Launcher::ServerDialog::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    appendRawLog(mProcess->readAllStandardOutput());
    appendRawLog(mProcess->readAllStandardError());

    QString backupError;
    const QString archivePath = createBackupArchive(&backupError);
    if (!archivePath.isEmpty())
        appendStatusLine(tr("Backup created: %1").arg(QDir::toNativeSeparators(archivePath)));
    else if (!backupError.isEmpty())
        appendStatusLine(tr("Backup failed: %1").arg(backupError));

    ++mRestartCounter;
    cleanupOldLogsIfNeeded();

    appendStatusLine(tr("Server stopped. Exit code: %1").arg(exitCode));

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool rapidCrash = !mStopRequested
        && exitStatus == QProcess::CrashExit
        && mLastStartMs > 0
        && (nowMs - mLastStartMs) < 15000;

    if (rapidCrash)
        ++mRapidCrashCount;
    else
        mRapidCrashCount = 0;

    if (!mStopRequested && mRestartCheckBox->isChecked())
    {
        if (mRapidCrashCount >= 3)
        {
            appendStatusLine(tr("Server crashed too many times in a short period. Auto restart disabled."));
            mRestartCheckBox->setChecked(false);
        }
        else
        {
            appendStatusLine(tr("Restarting server..."));
            startServer();
            return;
        }
    }

    if (exitStatus == QProcess::CrashExit && !mStopRequested)
    {
        appendStatusLine(tr("The server process crashed."));
    }

    mStopButton->setEnabled(false);
}

void Launcher::ServerDialog::processError(QProcess::ProcessError error)
{
    if (error == QProcess::Crashed)
        return;

    appendStatusLine(tr("Process error: %1").arg(mProcess->errorString()));
}

void Launcher::ServerDialog::stopServer()
{
    mStopRequested = true;
    mRestartCheckBox->setChecked(false);
    if (mProcess->state() == QProcess::NotRunning)
    {
        mStopButton->setEnabled(false);
            return;
    }

    appendStatusLine(tr("Stopping server..."));
    mProcess->terminate();
    if (!mProcess->waitForFinished(5000))
        mProcess->kill();
}

void Launcher::ServerDialog::refreshDecodedLog()
{
    updateLogView();
}

void Launcher::ServerDialog::appendRawLog(const QByteArray& data)
{
    if (data.isEmpty())
        return;

    mRawLog.append(data);
    updateLogView();
}

void Launcher::ServerDialog::updateLogView()
{
    QTextCodec* codec = currentCodec();
    const QString text = codec != nullptr ? codec->toUnicode(mRawLog) : QString::fromUtf8(mRawLog.constData(), mRawLog.size());
    mLogView->setPlainText(text);
    QTextCursor cursor = mLogView->textCursor();
    cursor.movePosition(QTextCursor::End);
    mLogView->setTextCursor(cursor);
    mLogView->ensureCursorVisible();
}

QTextCodec* Launcher::ServerDialog::currentCodec() const
{
    const QString codecName = mEncodingCombo->currentData().toString();
    if (codecName == QLatin1String("System"))
        return QTextCodec::codecForLocale();

    QTextCodec* codec = QTextCodec::codecForName(codecName.toUtf8());
    return codec != nullptr ? codec : QTextCodec::codecForLocale();
}

Launcher::ServerDialog::ServerConfig Launcher::ServerDialog::readServerConfig() const
{
    ServerConfig result;
    result.localAddress = QStringLiteral("0.0.0.0");
    result.port = QStringLiteral("25565");
    result.serverHomePath = QDir(applicationBasePath()).filePath(QStringLiteral("server"));
    result.configPath = QDir(applicationBasePath()).filePath(QStringLiteral("tes3mp-server.cfg"));

    QStringList configCandidates;
    configCandidates << QDir(applicationBasePath()).filePath(QStringLiteral("tes3mp-server.cfg"))
                     << QDir(applicationBasePath()).filePath(QStringLiteral("tes3mp-server-default.cfg"));

    for (const QString& configPath : configCandidates)
    {
        QFile file(configPath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        result.configPath = configPath;

        QString currentSection;
        QTextStream in(&file);
        while (!in.atEnd())
        {
            const QString rawLine = in.readLine().trimmed();
            if (rawLine.isEmpty() || rawLine.startsWith('#') || rawLine.startsWith(';'))
                continue;
            if (rawLine.startsWith('[') && rawLine.endsWith(']'))
            {
                currentSection = rawLine.mid(1, rawLine.size() - 2).trimmed();
                continue;
            }

            const int equalsPos = rawLine.indexOf('=');
            if (equalsPos < 0)
                continue;

            const QString key = rawLine.left(equalsPos).trimmed();
            const QString value = rawLine.mid(equalsPos + 1).trimmed();

            if (currentSection == QLatin1String("General"))
            {
                if (key == QLatin1String("localAddress"))
                    result.localAddress = value;
                else if (key == QLatin1String("port"))
                    result.port = value;
            }
            else if (currentSection == QLatin1String("Plugins") && key == QLatin1String("home"))
            {
                result.serverHomePath = value;
            }
        }
    }

    QDir baseDir(applicationBasePath());
    if (QDir::isRelativePath(result.serverHomePath))
        result.serverHomePath = baseDir.absoluteFilePath(result.serverHomePath);
    else
        result.serverHomePath = QDir(result.serverHomePath).absolutePath();

    result.configPath = QFileInfo(result.configPath).absoluteFilePath();
    return result;
}

QString Launcher::ServerDialog::resolveServerExecutable() const
{
    QDir dir(applicationBasePath());
#ifdef Q_OS_WIN
    const QString executable = dir.absoluteFilePath(QStringLiteral("tes3mp-server.exe"));
#else
    const QString executable = dir.absoluteFilePath(QStringLiteral("tes3mp-server"));
#endif
    return QFileInfo(executable).exists() ? executable : QString();
}

QString Launcher::ServerDialog::resolveDisplayAddress(const QString& bindAddress) const
{
    if (!bindAddress.isEmpty() && bindAddress != QLatin1String("0.0.0.0"))
        return bindAddress;

    foreach (const QNetworkInterface& iface, QNetworkInterface::allInterfaces())
    {
        if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning))
            continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)
            continue;

        foreach (const QNetworkAddressEntry& entry, iface.addressEntries())
        {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                return entry.ip().toString();
        }
    }

    return QStringLiteral("127.0.0.1");
}

QString Launcher::ServerDialog::applicationBasePath() const
{
    return QCoreApplication::applicationDirPath();
}

QString Launcher::ServerDialog::backupDirectoryPath() const
{
    QDir dir(applicationBasePath());
    const QString backupPath = dir.absoluteFilePath(QStringLiteral("Backup"));
    QDir().mkpath(backupPath);
    return backupPath;
}

QString Launcher::ServerDialog::makeBackupArchivePath() const
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy_MM_dd_HH_mm_ss"));
    return QDir(backupDirectoryPath()).absoluteFilePath(QStringLiteral("archive_") + timestamp + QStringLiteral(".zip"));
}

QString Launcher::ServerDialog::createBackupArchive(QString* errorMessage) const
{
    const ServerConfig config = readServerConfig();
    const QFileInfo sourceInfo(config.serverHomePath);
    if (!sourceInfo.exists() || !sourceInfo.isDir())
    {
        if (errorMessage)
            *errorMessage = tr("Server directory was not found: %1").arg(QDir::toNativeSeparators(config.serverHomePath));
        return QString();
    }

    const QString archivePath = makeBackupArchivePath();
    QProcess archiver;
    archiver.setWorkingDirectory(applicationBasePath());
#ifdef Q_OS_WIN
    QStringList arguments;
    arguments << QStringLiteral("-NoProfile")
              << QStringLiteral("-ExecutionPolicy") << QStringLiteral("Bypass")
              << QStringLiteral("-Command")
              << QStringLiteral("Compress-Archive -Path '%1\\*' -DestinationPath '%2' -Force")
                    .arg(QDir::toNativeSeparators(config.serverHomePath).replace("'", "''"),
                         QDir::toNativeSeparators(archivePath).replace("'", "''"));
    archiver.start(QStringLiteral("powershell.exe"), arguments);
#else
    QStringList arguments;
    arguments << QStringLiteral("-r") << archivePath << QDir(config.serverHomePath).dirName();
    archiver.setWorkingDirectory(QFileInfo(config.serverHomePath).absolutePath());
    archiver.start(QStringLiteral("zip"), arguments);
#endif

    if (!archiver.waitForStarted(3000))
    {
        if (errorMessage)
            *errorMessage = archiver.errorString();
        return QString();
    }

    archiver.waitForFinished(-1);
    if (archiver.exitStatus() != QProcess::NormalExit || archiver.exitCode() != 0)
    {
        if (errorMessage)
            *errorMessage = QString::fromLocal8Bit(archiver.readAllStandardError());
        return QString();
    }

    return archivePath;
}

void Launcher::ServerDialog::cleanupOldLogsIfNeeded()
{
    if (mRestartCounter < 5)
        return;

    mRestartCounter = 0;

    QDir logDir(QDir(applicationBasePath()).absoluteFilePath(QStringLiteral("userdata")));
    if (!logDir.exists())
        return;

    const QStringList logFiles = logDir.entryList(QStringList() << QStringLiteral("*.log"), QDir::Files);
    for (const QString& fileName : logFiles)
        logDir.remove(fileName);

    appendStatusLine(tr("Old log files were cleaned up."));
}

void Launcher::ServerDialog::appendStatusLine(const QString& text)
{
    const QString line = QStringLiteral("[")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        + QStringLiteral("] ") + text + QStringLiteral("\n");
    mRawLog.append(line.toUtf8());
    updateLogView();
}
