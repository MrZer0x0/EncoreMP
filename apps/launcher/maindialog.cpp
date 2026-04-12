#include "maindialog.hpp"

#include <components/version/version.hpp>
#include <components/misc/helpviewer.hpp>

#include <QDate>
#include <QMessageBox>
#include <QFontDatabase>
#include <QInputDialog>
#include <QFileDialog>
#include <QCloseEvent>
#include <QTextCodec>
#include <QLabel>
#include <QResizeEvent>
#include <QByteArray>

#include "playpage.hpp"
#include "graphicspage.hpp"
#include <QTextStream>
#include <QFile>
#include "datafilespage.hpp"
#include "settingspage.hpp"
#include "advancedpage.hpp"
#include "serverdialog.hpp"

using namespace Process;

void cfgError(const QString& title, const QString& msg) {
    QMessageBox msgBox;
    msgBox.setWindowTitle(title);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setText(msg);
    msgBox.exec();
}

Launcher::MainDialog::MainDialog(QWidget *parent)
    : QMainWindow(parent), mGameSettings (mCfgMgr)
{
    setupUi(this);

    mGameInvoker = new ProcessInvoker();
    mWizardInvoker = new ProcessInvoker();
    mServerDialog = new ServerDialog(this);
    mWatermarkLabel = new QLabel(centralwidget);
    const QByteArray watermarkEncoded = QByteArray("VEVTM01QIDAuOC4xIFplcjBDdXN0b20=");
    const QString watermarkText = QString::fromUtf8(QByteArray::fromBase64(watermarkEncoded));
    mWatermarkLabel->setText(watermarkText);
    mWatermarkLabel->setObjectName(QStringLiteral("zer0customWatermark"));
    mWatermarkLabel->setProperty("wm_b64", QString::fromUtf8(watermarkEncoded));
    mWatermarkLabel->setProperty("wm_guard", QString::number(qHash(QString::fromUtf8(watermarkEncoded))));
    mWatermarkLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    mWatermarkLabel->setStyleSheet(QStringLiteral("QLabel#zer0customWatermark { color: rgba(255, 255, 255, 88); font-size: 16px; font-weight: 600; background: transparent; }"));
    mWatermarkLabel->adjustSize();

    connect(mWizardInvoker->getProcess(), SIGNAL(started()),
            this, SLOT(wizardStarted()));

    connect(mWizardInvoker->getProcess(), SIGNAL(finished(int,QProcess::ExitStatus)),
            this, SLOT(wizardFinished(int,QProcess::ExitStatus)));

    iconWidget->setViewMode(QListView::IconMode);
    iconWidget->setWrapping(false);
    iconWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Just to be sure
    iconWidget->setIconSize(QSize(48, 48));
    iconWidget->setMovement(QListView::Static);

    iconWidget->setSpacing(4);
    iconWidget->setCurrentRow(0);
    iconWidget->setFlow(QListView::LeftToRight);

    QPushButton *helpButton = new QPushButton(tr("Help"));
    QPushButton *playButton = new QPushButton(tr("Play"));
    QPushButton *serverButton = new QPushButton(tr("Run Server"));
    buttonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));
    buttonBox->addButton(helpButton, QDialogButtonBox::HelpRole);
    buttonBox->addButton(serverButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(playButton, QDialogButtonBox::AcceptRole);

    connect(buttonBox, SIGNAL(rejected()), this, SLOT(close()));
    connect(buttonBox, SIGNAL(accepted()), this, SLOT(play()));
    connect(serverButton, SIGNAL(clicked()), this, SLOT(runServer()));
    connect(buttonBox, SIGNAL(helpRequested()), this, SLOT(help()));

    // Remove what's this? button
    setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);

    createIcons();
    updateWatermarkPosition();
}

Launcher::MainDialog::~MainDialog()
{
    delete mGameInvoker;
    delete mWizardInvoker;
}

void Launcher::MainDialog::createIcons()
{
    if (!QIcon::hasThemeIcon("document-new"))
        QIcon::setThemeName("tango");

    QListWidgetItem *playButton = new QListWidgetItem(iconWidget);
    playButton->setIcon(QIcon(":/images/openmw.png"));
    playButton->setText(tr("Play"));
    playButton->setTextAlignment(Qt::AlignCenter);
    playButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *dataFilesButton = new QListWidgetItem(iconWidget);
    dataFilesButton->setIcon(QIcon(":/images/openmw-plugin.png"));
    dataFilesButton->setText(tr("Data Files"));
    dataFilesButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    dataFilesButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *graphicsButton = new QListWidgetItem(iconWidget);
    graphicsButton->setIcon(QIcon(":/images/preferences-video.png"));
    graphicsButton->setText(tr("Graphics"));
    graphicsButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom | Qt::AlignAbsolute);
    graphicsButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *settingsButton = new QListWidgetItem(iconWidget);
    settingsButton->setIcon(QIcon(":/images/preferences.png"));
    settingsButton->setText(tr("Settings"));
    settingsButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    settingsButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *advancedButton = new QListWidgetItem(iconWidget);
    advancedButton->setIcon(QIcon(":/images/preferences-advanced.png"));
    advancedButton->setText(tr("Advanced"));
    advancedButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    advancedButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    connect(iconWidget,
            SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)),
            this, SLOT(changePage(QListWidgetItem*,QListWidgetItem*)));

}

void Launcher::MainDialog::createPages()
{
    // Avoid creating the widgets twice
    if (pagesWidget->count() != 0)
        return;

    mPlayPage = new PlayPage(this);
    mDataFilesPage = new DataFilesPage(mCfgMgr, mGameSettings, mLauncherSettings, this);
    mGraphicsPage = new GraphicsPage(this);
    mSettingsPage = new SettingsPage(mCfgMgr, mGameSettings, mLauncherSettings, this);
    mAdvancedPage = new AdvancedPage(mGameSettings, this);
    mPlayPage->setServerConsoleWidget(mServerDialog);

    {
        QString cfgPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str()) + "/tes3mp-client-default.cfg";
        QFile cfgFile(cfgPath);
        QString addr = "localhost", port = "25565";
        if (cfgFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&cfgFile);
            while (!in.atEnd())
            {
                QString line = in.readLine().trimmed();
                if (line.startsWith("destinationAddress"))
                    addr = line.section('=', 1).trimmed();
                else if (line.startsWith("port") && !line.startsWith("password"))
                    port = line.section('=', 1).trimmed();
            }
        }
        mPlayPage->setServerAddress(addr);
        mPlayPage->setServerPort(port);
    }

    // Add the pages to the stacked widget
    pagesWidget->addWidget(mPlayPage);
    pagesWidget->addWidget(mDataFilesPage);
    pagesWidget->addWidget(mGraphicsPage);
    pagesWidget->addWidget(mSettingsPage);
    pagesWidget->addWidget(mAdvancedPage);

    // Select the first page
    iconWidget->setCurrentItem(iconWidget->item(0), QItemSelectionModel::Select);

    connect(mPlayPage, SIGNAL(playButtonClicked()), this, SLOT(play()));
    connect(mPlayPage, SIGNAL(serverButtonClicked()), this, SLOT(runServer()));

    // Using Qt::QueuedConnection because signal is emitted in a subthread and slot is in the main thread
    connect(mDataFilesPage, SIGNAL(signalLoadedCellsChanged(QStringList)), mAdvancedPage, SLOT(slotLoadedCellsChanged(QStringList)), Qt::QueuedConnection);

}

Launcher::FirstRunDialogResult Launcher::MainDialog::showFirstRunDialog()
{
    if (!setupLauncherSettings())
        return FirstRunDialogResultFailure;

    if (mLauncherSettings.value(QString("General/firstrun"), QString("true")) == QLatin1String("true"))
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("First run"));
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setStandardButtons(QMessageBox::NoButton);
        msgBox.setText(tr("<html><head/><body><p><b>Welcome to OpenMW!</b></p> \
                          <p>It is recommended to run the Installation Wizard.</p> \
                          <p>The Wizard will let you select an existing Morrowind installation, \
                          or install Morrowind for OpenMW to use.</p></body></html>"));

        QAbstractButton *wizardButton =
                msgBox.addButton(tr("Run &Installation Wizard"), QMessageBox::AcceptRole); // ActionRole doesn't work?!
        QAbstractButton *skipButton =
                msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == wizardButton)
        {
            if (mWizardInvoker->startProcess(QLatin1String("openmw-wizard"), false))
                return FirstRunDialogResultWizard;
        }
        else if (msgBox.clickedButton() == skipButton)
        {
            // Don't bother setting up absent game data.
            if (setup())
                return FirstRunDialogResultContinue;
        }
        return FirstRunDialogResultFailure;
    }

    if (!setup() || !setupGameData()) {
        return FirstRunDialogResultFailure;
    }
    return FirstRunDialogResultContinue;
}

void Launcher::MainDialog::setVersionLabel()
{
    versionLabel->setText(QStringLiteral("TES3MP 0.8.1 Zer0Custom"));
}

bool Launcher::MainDialog::setup()
{
    if (!setupGameSettings())
        return false;

    setVersionLabel();

    mLauncherSettings.setContentList(mGameSettings);

    if (!setupGraphicsSettings())
        return false;

    // Now create the pages as they need the settings
    createPages();

    // Call this so we can exit on SDL errors before mainwindow is shown
    if (!mGraphicsPage->loadSettings())
        return false;

    loadSettings();

    return true;
}

bool Launcher::MainDialog::reloadSettings()
{
    if (!setupLauncherSettings())
        return false;

    if (!setupGameSettings())
        return false;

    mLauncherSettings.setContentList(mGameSettings);

    if (!setupGraphicsSettings())
        return false;

    if (!mSettingsPage->loadSettings())
        return false;

    if (!mDataFilesPage->loadSettings())
        return false;

    if (!mGraphicsPage->loadSettings())
        return false;

    if (!mAdvancedPage->loadSettings())
        return false;

    return true;
}

void Launcher::MainDialog::changePage(QListWidgetItem *current, QListWidgetItem *previous)
{
    if (!current)
        current = previous;

    int currentIndex = iconWidget->row(current);
    pagesWidget->setCurrentIndex(currentIndex);
    mSettingsPage->resetProgressBar();
}

bool Launcher::MainDialog::setupLauncherSettings()
{
    mLauncherSettings.clear();

    mLauncherSettings.setMultiValueEnabled(true);

    QString userPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str());
    QDir userDir(userPath);

    QStringList paths;
    paths.append(QString(Config::LauncherSettings::sLauncherConfigFileName));
    paths.append(userDir.filePath(QString(Config::LauncherSettings::sLauncherConfigFileName)));

    for (const QString &path : paths)
    {
        qDebug() << "Loading config file:" << path.toUtf8().constData();
        QFile file(path);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                cfgError(tr("Error opening OpenMW configuration file"),
                         tr("<br><b>Could not open %0 for reading</b><br><br> \
                             Please make sure you have the right permissions \
                             and try again.<br>").arg(file.fileName()));
                return false;
            }
            QTextStream stream(&file);
            stream.setCodec(QTextCodec::codecForName("UTF-8"));

            mLauncherSettings.readFile(stream);
        }
        file.close();
    }

    return true;
}

bool Launcher::MainDialog::setupGameSettings()
{
    mGameSettings.clear();

    QString localPath = QString::fromUtf8(mCfgMgr.getLocalPath().string().c_str());
    QString userPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str());
    QString globalPath = QString::fromUtf8(mCfgMgr.getGlobalPath().string().c_str());
    QDir localDir(localPath);
    QDir userDir(userPath);
    QDir globalDir(globalPath);

    // Load the user config file first, separately
    // So we can write it properly, uncontaminated
    QString path = userDir.filePath(QLatin1String("openmw.cfg"));
    QFile file(path);

    qDebug() << "Loading config file:" << path.toUtf8().constData();

    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            cfgError(tr("Error opening OpenMW configuration file"),
                     tr("<br><b>Could not open %0 for reading</b><br><br> \
                         Please make sure you have the right permissions \
                         and try again.<br>").arg(file.fileName()));
            return false;
        }
        QTextStream stream(&file);
        stream.setCodec(QTextCodec::codecForName("UTF-8"));

        mGameSettings.readUserFile(stream);
        file.close();
    }

    // Now the rest - priority: user > local > global
    QStringList paths;
    paths.append(globalDir.filePath(QString("openmw.cfg")));
    paths.append(localDir.filePath(QString("openmw.cfg")));
    paths.append(userDir.filePath(QString("openmw.cfg")));

    for (const QString &path2 : paths)
    {
        qDebug() << "Loading config file:" << path2.toUtf8().constData();

        file.setFileName(path2);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                cfgError(tr("Error opening OpenMW configuration file"),
                         tr("<br><b>Could not open %0 for reading</b><br><br> \
                             Please make sure you have the right permissions \
                             and try again.<br>").arg(file.fileName()));
                return false;
            }
            QTextStream stream(&file);
            stream.setCodec(QTextCodec::codecForName("UTF-8"));

            mGameSettings.readFile(stream);
            file.close();
        }
    }

    return true;
}

bool Launcher::MainDialog::setupGameData()
{
    QStringList dataDirs;

    // Check if the paths actually contain data files
    for (const QString& path3 : mGameSettings.getDataDirs())
    {
        QDir dir(path3);
        QStringList filters;
        filters << "*.esp" << "*.esm" << "*.omwgame" << "*.omwaddon";

        if (!dir.entryList(filters).isEmpty())
            dataDirs.append(path3);
    }

    if (dataDirs.isEmpty())
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error detecting Morrowind installation"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::NoButton);
        msgBox.setText(tr("<br><b>Could not find the Data Files location</b><br><br> \
                                   The directory containing the data files was not found."));

        QAbstractButton *wizardButton =
                msgBox.addButton(tr("Run &Installation Wizard..."), QMessageBox::ActionRole);
        QAbstractButton *skipButton =
                msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);

        Q_UNUSED(skipButton); // Suppress compiler unused warning

        msgBox.exec();

        if (msgBox.clickedButton() == wizardButton)
        {
            if (!mWizardInvoker->startProcess(QLatin1String("openmw-wizard"), false))
                return false;
        }
    }

    return true;
}

bool Launcher::MainDialog::setupGraphicsSettings()
{
    // This method is almost a copy of OMW::Engine::loadSettings().  They should definitely
    // remain consistent, and possibly be merged into a shared component.  At the very least
    // the filenames should be in the CfgMgr component.

    // Ensure to clear previous settings in case we had already loaded settings.
    mEngineSettings.clear();
    // Load default settings. Prefer defaults.bin when available, but also
    // support a plain settings-default.cfg for portable/client-only builds.
    const std::string localDefault = (mCfgMgr.getLocalPath() / "defaults.bin").string();
    const std::string globalDefault = (mCfgMgr.getGlobalPath() / "defaults.bin").string();
    const std::string localDefaultCfg = (mCfgMgr.getLocalPath() / "settings-default.cfg").string();
    const std::string globalDefaultCfg = (mCfgMgr.getGlobalPath() / "settings-default.cfg").string();
    std::string defaultPath;
    bool defaultIsTextCfg = false;

    if (boost::filesystem::exists(localDefault))
        defaultPath = localDefault;
    else if (boost::filesystem::exists(globalDefault))
        defaultPath = globalDefault;
    else if (boost::filesystem::exists(localDefaultCfg))
    {
        defaultPath = localDefaultCfg;
        defaultIsTextCfg = true;
    }
    else if (boost::filesystem::exists(globalDefaultCfg))
    {
        defaultPath = globalDefaultCfg;
        defaultIsTextCfg = true;
    }
    else {
        cfgError(tr("Error reading OpenMW configuration file"),
                 tr("<br><b>Could not find defaults.bin or settings-default.cfg</b><br><br>                      The problem may be due to an incomplete installation of OpenMW.<br>                      Reinstalling OpenMW may resolve the problem."));
        return false;
    }

    try {
        mEngineSettings.loadDefault(defaultPath, !defaultIsTextCfg);
    }
    catch (std::exception& e) {
        std::string msg = std::string("<br><b>Error reading default settings</b><br><br>") + e.what();
        cfgError(tr("Error reading OpenMW configuration file"), tr(msg.c_str()));
        return false;
    }

    // Load user settings if they exist
    const std::string userPath = (mCfgMgr.getUserConfigPath() / "settings.cfg").string();
    // User settings are not required to exist, so if they don't we're done.
    if (!boost::filesystem::exists(userPath)) return true;

    try {
        mEngineSettings.loadUser(userPath);
    }
    catch (std::exception& e) {
        std::string msg = std::string("<br><b>Error reading settings.cfg</b><br><br>") + e.what();
        cfgError(tr("Error reading OpenMW configuration file"), tr(msg.c_str()));
        return false;
    }

    return true;
}

void Launcher::MainDialog::loadSettings()
{
    int width = mLauncherSettings.value(QString("General/MainWindow/width")).toInt();
    int height = mLauncherSettings.value(QString("General/MainWindow/height")).toInt();

    int posX = mLauncherSettings.value(QString("General/MainWindow/posx")).toInt();
    int posY = mLauncherSettings.value(QString("General/MainWindow/posy")).toInt();

    resize(width, height);
    move(posX, posY);
}

void Launcher::MainDialog::saveSettings()
{
    QString width = QString::number(this->width());
    QString height = QString::number(this->height());

    mLauncherSettings.setValue(QString("General/MainWindow/width"), width);
    mLauncherSettings.setValue(QString("General/MainWindow/height"), height);

    QString posX = QString::number(this->pos().x());
    QString posY = QString::number(this->pos().y());

    mLauncherSettings.setValue(QString("General/MainWindow/posx"), posX);
    mLauncherSettings.setValue(QString("General/MainWindow/posy"), posY);

    mLauncherSettings.setValue(QString("General/firstrun"), QString("false"));

}

bool Launcher::MainDialog::writeSettings()
{
    // Now write all config files
    saveSettings();
    mDataFilesPage->saveSettings();
    mGraphicsPage->saveSettings();
    mSettingsPage->saveSettings();
    mAdvancedPage->saveSettings();
    mPlayPage->saveServerSettings();

    QString userPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str());
    QDir dir(userPath);

    if (!dir.exists()) {
        if (!dir.mkpath(userPath)) {
            cfgError(tr("Error creating OpenMW configuration directory"),
                     tr("<br><b>Could not create %0</b><br><br> \
                         Please make sure you have the right permissions \
                         and try again.<br>").arg(userPath));
            return false;
        }
    }

    // Game settings
    QFile file(dir.filePath(QString("openmw.cfg")));

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        // File cannot be opened or created
        cfgError(tr("Error writing OpenMW configuration file"),
                 tr("<br><b>Could not open or create %0 for writing</b><br><br> \
                     Please make sure you have the right permissions \
                     and try again.<br>").arg(file.fileName()));
        return false;
    }


    mGameSettings.writeFileWithComments(file);
    file.close();

    // Graphics settings
    const std::string settingsPath = (mCfgMgr.getUserConfigPath() / "settings.cfg").string();
    try {
        mEngineSettings.saveUser(settingsPath);
    }
    catch (std::exception& e) {
        std::string msg = "<br><b>Error writing settings.cfg</b><br><br>" +
            settingsPath + "<br><br>" + e.what();
        cfgError(tr("Error writing user settings file"), tr(msg.c_str()));
        return false;
    }

    // Launcher settings
    file.setFileName(dir.filePath(QString(Config::LauncherSettings::sLauncherConfigFileName)));

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text | QIODevice::Truncate)) {
        // File cannot be opened or created
        cfgError(tr("Error writing Launcher configuration file"),
                 tr("<br><b>Could not open or create %0 for writing</b><br><br> \
                     Please make sure you have the right permissions \
                     and try again.<br>").arg(file.fileName()));
        return false;
    }

    QTextStream stream(&file);
    stream.setDevice(&file);
    stream.setCodec(QTextCodec::codecForName("UTF-8"));

    mLauncherSettings.writeFile(stream);
    file.close();

    return true;
}

void Launcher::MainDialog::updateWatermarkPosition()
{
    if (mWatermarkLabel == nullptr)
        return;

    mWatermarkLabel->adjustSize();
    const int margin = 14;
    const QSize size = mWatermarkLabel->sizeHint();
    mWatermarkLabel->move(centralwidget->width() - size.width() - margin,
                          centralwidget->height() - size.height() - margin);
    mWatermarkLabel->raise();
}

void Launcher::MainDialog::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateWatermarkPosition();
}

void Launcher::MainDialog::closeEvent(QCloseEvent *event)
{
    writeSettings();
    event->accept();
}

void Launcher::MainDialog::wizardStarted()
{
    hide();
}

void Launcher::MainDialog::wizardFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus == QProcess::CrashExit)
        return qApp->quit();

    // HACK: Ensure the pages are created, else segfault
    setup();

    if (setupGameData() && reloadSettings())
    {
        show();
        raise();
        activateWindow();
    }
}

void Launcher::MainDialog::play()
{
    if (!writeSettings())
        return qApp->quit();

    if (!mGameSettings.hasMaster()) {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("No game file selected"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(tr("<br><b>You do not have a game file selected.</b><br><br> \
                          OpenMW will not start without a game file selected.<br>"));
                          msgBox.exec();
        return;
    }

    {
        QString cfgPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str()) + "/tes3mp-client-default.cfg";
        QFile cfgFile(cfgPath);
        QStringList lines;
        bool foundAddr = false, foundPort = false;
        if (cfgFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&cfgFile);
            while (!in.atEnd()) lines << in.readLine();
            cfgFile.close();
        }
        const QString newAddr = mPlayPage->serverAddress();
        const QString newPort = mPlayPage->serverPort();
        for (QString& line : lines)
        {
            const QString t = line.trimmed();
            if (t.startsWith("destinationAddress"))
            {
                line = "destinationAddress = " + newAddr;
                foundAddr = true;
            }
            else if (t.startsWith("port") && !t.startsWith("password"))
            {
                line = "port = " + newPort;
                foundPort = true;
            }
        }
        if (!foundAddr) lines << "destinationAddress = " + newAddr;
        if (!foundPort) lines << "port = " + newPort;
        if (cfgFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            QTextStream out(&cfgFile);
            for (const QString& line : lines) out << line << "\n";
        }
    }

    QStringList arguments;
    arguments.append(QLatin1String("--connect=") + mPlayPage->serverAddress() + QLatin1String(":") + mPlayPage->serverPort());

    if (mGameInvoker->startProcess(QLatin1String("tes3mp"), arguments, true))
    {
        if (mServerDialog != nullptr && mServerDialog->isRunning())
            return;

        return qApp->quit();
    }
}

void Launcher::MainDialog::runServer()
{
    if (!writeSettings())
        return;

    mPlayPage->saveServerSettings();
    mPlayPage->switchToServerConsoleTab();
    mServerDialog->startServer();
}

void Launcher::MainDialog::help()
{
    Misc::HelpViewer::openHelp("reference/index.html");
}
