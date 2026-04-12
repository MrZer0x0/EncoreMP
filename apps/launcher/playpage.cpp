#include "playpage.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTextStream>
#include <QVBoxLayout>

namespace
{
    bool findConfigAssignment(const QString& text, const QString& key, QString* value)
    {
        const QRegularExpression pattern(QStringLiteral("(^|\\n)\\s*config\\.%1\\s*=\\s*([^\\r\\n]+)").arg(QRegularExpression::escape(key)));
        const QRegularExpressionMatch match = pattern.match(text);
        if (!match.hasMatch())
            return false;

        if (value != nullptr)
            *value = match.captured(2).trimmed();
        return true;
    }

    QString replaceConfigAssignment(const QString& text, const QString& key, const QString& value)
    {
        const QRegularExpression pattern(QStringLiteral("(^|\\n)(\\s*config\\.%1\\s*=\\s*)([^\\r\\n]+)").arg(QRegularExpression::escape(key)));
        QString result = text;
        result.replace(pattern, QStringLiteral("\\1\\2") + value);
        return result;
    }

    void loadLineEdit(QLineEdit* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2)
            value = value.mid(1, value.size() - 2);

        widget->setText(value);
    }

    void loadSpinBox(QSpinBox* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        bool ok = false;
        const int parsed = value.toInt(&ok);
        if (ok)
            widget->setValue(parsed);
    }

    void loadCheckBox(QCheckBox* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        widget->setChecked(value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
    }

    void loadComboBox(QComboBox* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2)
            value = value.mid(1, value.size() - 2);

        const int index = widget->findData(value);
        if (index >= 0)
            widget->setCurrentIndex(index);
    }
}

Launcher::PlayPage::PlayPage(QWidget *parent)
    : QWidget(parent)
    , mEmbeddedServerConsole(nullptr)
{
    setObjectName("PlayPage");
    setupUi(this);

    connect(playButton, SIGNAL(clicked()), this, SLOT(slotPlayClicked()));
    connect(serverButton, SIGNAL(clicked()), this, SLOT(slotServerClicked()));
    connect(reloadServerSettingsButton, SIGNAL(clicked()), this, SLOT(slotReloadServerSettings()));
    connect(saveServerSettingsButton, SIGNAL(clicked()), this, SLOT(slotSaveServerSettings()));
    connect(applyServerSettingsFormButton, SIGNAL(clicked()), this, SLOT(slotApplyFormToRawConfig()));
    connect(syncServerSettingsFormButton, SIGNAL(clicked()), this, SLOT(slotSyncFormFromRawConfig()));

    pageTabs->setCurrentIndex(0);
    serverSettingsModeTabs->setCurrentIndex(0);
    loadServerSettings();
}

void Launcher::PlayPage::setServerAddress(const QString& addr)
{
    serverAddressEdit->setText(addr);
}

void Launcher::PlayPage::setServerPort(const QString& port)
{
    serverPortEdit->setText(port);
}

void Launcher::PlayPage::setServerConsoleWidget(QWidget* widget)
{
    if (widget == nullptr)
        return;

    if (mEmbeddedServerConsole == widget)
        return;

    if (mEmbeddedServerConsole != nullptr)
    {
        serverConsoleHostLayout->removeWidget(mEmbeddedServerConsole);
        mEmbeddedServerConsole->setParent(nullptr);
    }

    mEmbeddedServerConsole = widget;
    widget->setParent(serverConsoleHost);
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    serverConsoleHostLayout->addWidget(widget);
}

QString Launcher::PlayPage::serverAddress() const
{
    QString addr = serverAddressEdit->text().trimmed();
    return addr.isEmpty() ? QString("localhost") : addr;
}

QString Launcher::PlayPage::serverPort() const
{
    QString p = serverPortEdit->text().trimmed();
    return p.isEmpty() ? QString("25565") : p;
}

void Launcher::PlayPage::switchToServerConsoleTab()
{
    pageTabs->setCurrentWidget(serverConsoleTab);
}

QString Launcher::PlayPage::serverConfigPath() const
{
    const QDir baseDir(QApplication::applicationDirPath());
    return QDir::cleanPath(baseDir.filePath(QStringLiteral("server/scripts/config.lua")));
}

QString Launcher::PlayPage::replaceRawValue(const QString& text, const QString& key, const QString& value) const
{
    return replaceConfigAssignment(text, key, value);
}

void Launcher::PlayPage::setServerSettingsStatus(const QString& text, bool isError)
{
    serverSettingsStatusLabel->setText(text);
    serverSettingsStatusLabel->setStyleSheet(isError
        ? QStringLiteral("color: #9f1f1f; font-weight: 600;")
        : QStringLiteral("color: #245027; font-weight: 600;"));
}

void Launcher::PlayPage::populateFormFromConfig(const QString& text)
{
    loadLineEdit(gameModeEdit, text, QStringLiteral("gameMode"));
    loadLineEdit(dataPathEdit, text, QStringLiteral("dataPath"));

    loadSpinBox(loginTimeSpinBox, text, QStringLiteral("loginTime"));
    loadSpinBox(maxClientsPerIPSpinBox, text, QStringLiteral("maxClientsPerIP"));
    loadSpinBox(difficultySpinBox, text, QStringLiteral("difficulty"));
    loadSpinBox(nightStartHourSpinBox, text, QStringLiteral("nightStartHour"));
    loadSpinBox(nightEndHourSpinBox, text, QStringLiteral("nightEndHour"));
    loadSpinBox(deathTimeSpinBox, text, QStringLiteral("deathTime"));
    loadSpinBox(deathPenaltyJailDaysSpinBox, text, QStringLiteral("deathPenaltyJailDays"));
    loadSpinBox(fixmeIntervalSpinBox, text, QStringLiteral("fixmeInterval"));
    loadSpinBox(pingDifferenceSpinBox, text, QStringLiteral("pingDifferenceRequiredForAuthority"));
    loadSpinBox(enforcedLogLevelSpinBox, text, QStringLiteral("enforcedLogLevel"));
    loadSpinBox(physicsFramerateSpinBox, text, QStringLiteral("physicsFramerate"));

    loadCheckBox(passTimeWhenEmptyCheckBox, text, QStringLiteral("passTimeWhenEmpty"));
    loadCheckBox(allowConsoleCheckBox, text, QStringLiteral("allowConsole"));
    loadCheckBox(allowBedRestCheckBox, text, QStringLiteral("allowBedRest"));
    loadCheckBox(allowWildernessRestCheckBox, text, QStringLiteral("allowWildernessRest"));
    loadCheckBox(allowWaitCheckBox, text, QStringLiteral("allowWait"));
    loadCheckBox(useInstancedSpawnCheckBox, text, QStringLiteral("useInstancedSpawn"));
    loadCheckBox(respawnAtImperialShrineCheckBox, text, QStringLiteral("respawnAtImperialShrine"));
    loadCheckBox(respawnAtTribunalTempleCheckBox, text, QStringLiteral("respawnAtTribunalTemple"));
    loadCheckBox(playersRespawnCheckBox, text, QStringLiteral("playersRespawn"));
    loadCheckBox(bountyResetOnDeathCheckBox, text, QStringLiteral("bountyResetOnDeath"));
    loadCheckBox(bountyDeathPenaltyCheckBox, text, QStringLiteral("bountyDeathPenalty"));
    loadCheckBox(allowSuicideCommandCheckBox, text, QStringLiteral("allowSuicideCommand"));
    loadCheckBox(allowFixmeCommandCheckBox, text, QStringLiteral("allowFixmeCommand"));
    loadCheckBox(allowOnContainerForUnloadedCellsCheckBox, text, QStringLiteral("allowOnContainerForUnloadedCells"));
    loadCheckBox(enablePlayerCollisionCheckBox, text, QStringLiteral("enablePlayerCollision"));
    loadCheckBox(enableActorCollisionCheckBox, text, QStringLiteral("enableActorCollision"));
    loadCheckBox(enablePlacedObjectCollisionCheckBox, text, QStringLiteral("enablePlacedObjectCollision"));
    loadCheckBox(useActorCollisionForPlacedObjectsCheckBox, text, QStringLiteral("useActorCollisionForPlacedObjects"));
    loadCheckBox(enforceDataFilesCheckBox, text, QStringLiteral("enforceDataFiles"));
    loadCheckBox(ignoreScriptErrorsCheckBox, text, QStringLiteral("ignoreScriptErrors"));

    loadCheckBox(shareJournalCheckBox, text, QStringLiteral("shareJournal"));
    loadCheckBox(shareFactionRanksCheckBox, text, QStringLiteral("shareFactionRanks"));
    loadCheckBox(shareFactionExpulsionCheckBox, text, QStringLiteral("shareFactionExpulsion"));
    loadCheckBox(shareFactionReputationCheckBox, text, QStringLiteral("shareFactionReputation"));
    loadCheckBox(shareTopicsCheckBox, text, QStringLiteral("shareTopics"));
    loadCheckBox(shareBountyCheckBox, text, QStringLiteral("shareBounty"));
    loadCheckBox(shareReputationCheckBox, text, QStringLiteral("shareReputation"));
    loadCheckBox(shareMapExplorationCheckBox, text, QStringLiteral("shareMapExploration"));
    loadCheckBox(shareVideosCheckBox, text, QStringLiteral("shareVideos"));
    loadCheckBox(shareKillsCheckBox, text, QStringLiteral("shareKills"));

    loadComboBox(databaseTypeComboBox, text, QStringLiteral("databaseType"));
}

QString Launcher::PlayPage::updatedConfigFromForm(const QString& input) const
{
    QString text = input;

    const auto replaceString = [&text](const QString& key, const QString& value)
    {
        text = replaceConfigAssignment(text, key, QStringLiteral("\"") + value + QStringLiteral("\""));
    };

    const auto replaceNumber = [&text](const QString& key, int value)
    {
        text = replaceConfigAssignment(text, key, QString::number(value));
    };

    const auto replaceBool = [&text](const QString& key, bool value)
    {
        text = replaceConfigAssignment(text, key, value ? QStringLiteral("true") : QStringLiteral("false"));
    };

    replaceString(QStringLiteral("gameMode"), gameModeEdit->text().trimmed());

    const QString dataPathValue = dataPathEdit->text().trimmed();
    if (dataPathValue == QStringLiteral("tes3mp.GetDataPath()"))
        text = replaceRawValue(text, QStringLiteral("dataPath"), dataPathValue);
    else
        replaceString(QStringLiteral("dataPath"), dataPathValue);

    replaceNumber(QStringLiteral("loginTime"), loginTimeSpinBox->value());
    replaceNumber(QStringLiteral("maxClientsPerIP"), maxClientsPerIPSpinBox->value());
    replaceNumber(QStringLiteral("difficulty"), difficultySpinBox->value());
    replaceNumber(QStringLiteral("nightStartHour"), nightStartHourSpinBox->value());
    replaceNumber(QStringLiteral("nightEndHour"), nightEndHourSpinBox->value());
    replaceNumber(QStringLiteral("deathTime"), deathTimeSpinBox->value());
    replaceNumber(QStringLiteral("deathPenaltyJailDays"), deathPenaltyJailDaysSpinBox->value());
    replaceNumber(QStringLiteral("fixmeInterval"), fixmeIntervalSpinBox->value());
    replaceNumber(QStringLiteral("pingDifferenceRequiredForAuthority"), pingDifferenceSpinBox->value());
    replaceNumber(QStringLiteral("enforcedLogLevel"), enforcedLogLevelSpinBox->value());
    replaceNumber(QStringLiteral("physicsFramerate"), physicsFramerateSpinBox->value());

    replaceBool(QStringLiteral("passTimeWhenEmpty"), passTimeWhenEmptyCheckBox->isChecked());
    replaceBool(QStringLiteral("allowConsole"), allowConsoleCheckBox->isChecked());
    replaceBool(QStringLiteral("allowBedRest"), allowBedRestCheckBox->isChecked());
    replaceBool(QStringLiteral("allowWildernessRest"), allowWildernessRestCheckBox->isChecked());
    replaceBool(QStringLiteral("allowWait"), allowWaitCheckBox->isChecked());
    replaceBool(QStringLiteral("useInstancedSpawn"), useInstancedSpawnCheckBox->isChecked());
    replaceBool(QStringLiteral("respawnAtImperialShrine"), respawnAtImperialShrineCheckBox->isChecked());
    replaceBool(QStringLiteral("respawnAtTribunalTemple"), respawnAtTribunalTempleCheckBox->isChecked());
    replaceBool(QStringLiteral("playersRespawn"), playersRespawnCheckBox->isChecked());
    replaceBool(QStringLiteral("bountyResetOnDeath"), bountyResetOnDeathCheckBox->isChecked());
    replaceBool(QStringLiteral("bountyDeathPenalty"), bountyDeathPenaltyCheckBox->isChecked());
    replaceBool(QStringLiteral("allowSuicideCommand"), allowSuicideCommandCheckBox->isChecked());
    replaceBool(QStringLiteral("allowFixmeCommand"), allowFixmeCommandCheckBox->isChecked());
    replaceBool(QStringLiteral("allowOnContainerForUnloadedCells"), allowOnContainerForUnloadedCellsCheckBox->isChecked());
    replaceBool(QStringLiteral("enablePlayerCollision"), enablePlayerCollisionCheckBox->isChecked());
    replaceBool(QStringLiteral("enableActorCollision"), enableActorCollisionCheckBox->isChecked());
    replaceBool(QStringLiteral("enablePlacedObjectCollision"), enablePlacedObjectCollisionCheckBox->isChecked());
    replaceBool(QStringLiteral("useActorCollisionForPlacedObjects"), useActorCollisionForPlacedObjectsCheckBox->isChecked());
    replaceBool(QStringLiteral("enforceDataFiles"), enforceDataFilesCheckBox->isChecked());
    replaceBool(QStringLiteral("ignoreScriptErrors"), ignoreScriptErrorsCheckBox->isChecked());

    replaceBool(QStringLiteral("shareJournal"), shareJournalCheckBox->isChecked());
    replaceBool(QStringLiteral("shareFactionRanks"), shareFactionRanksCheckBox->isChecked());
    replaceBool(QStringLiteral("shareFactionExpulsion"), shareFactionExpulsionCheckBox->isChecked());
    replaceBool(QStringLiteral("shareFactionReputation"), shareFactionReputationCheckBox->isChecked());
    replaceBool(QStringLiteral("shareTopics"), shareTopicsCheckBox->isChecked());
    replaceBool(QStringLiteral("shareBounty"), shareBountyCheckBox->isChecked());
    replaceBool(QStringLiteral("shareReputation"), shareReputationCheckBox->isChecked());
    replaceBool(QStringLiteral("shareMapExploration"), shareMapExplorationCheckBox->isChecked());
    replaceBool(QStringLiteral("shareVideos"), shareVideosCheckBox->isChecked());
    replaceBool(QStringLiteral("shareKills"), shareKillsCheckBox->isChecked());

    const QString databaseTypeValue = databaseTypeComboBox->currentData().toString();
    if (!databaseTypeValue.isEmpty())
        replaceString(QStringLiteral("databaseType"), databaseTypeValue);

    return text;
}

void Launcher::PlayPage::loadServerSettings()
{
    const QString path = serverConfigPath();
    serverSettingsPathLabel->setText(QDir::toNativeSeparators(path));

    QFile file(path);
    if (!file.exists())
    {
        serverSettingsEditor->setPlainText(QString());
        setServerSettingsStatus(tr("config.lua not found"), true);
        return;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        setServerSettingsStatus(tr("Could not open config.lua"), true);
        return;
    }

    QTextStream stream(&file);
    const QString text = stream.readAll();
    serverSettingsEditor->setPlainText(text);
    populateFormFromConfig(text);
    setServerSettingsStatus(tr("Loaded config.lua"));
}

bool Launcher::PlayPage::saveServerSettings()
{
    const QString path = serverConfigPath();
    QFileInfo info(path);
    QDir dir = info.absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        QMessageBox::warning(this, tr("Save error"), tr("Could not create directory for config.lua"));
        setServerSettingsStatus(tr("Could not create directory"), true);
        return false;
    }

    if (serverSettingsModeTabs->currentWidget() == formServerSettingsTab)
        serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        QMessageBox::warning(this, tr("Save error"), tr("Could not write config.lua"));
        setServerSettingsStatus(tr("Could not write config.lua"), true);
        return false;
    }

    QTextStream stream(&file);
    stream << serverSettingsEditor->toPlainText();
    file.close();

    setServerSettingsStatus(tr("Saved config.lua"));
    return true;
}

void Launcher::PlayPage::slotPlayClicked()
{
    emit playButtonClicked();
}

void Launcher::PlayPage::slotServerClicked()
{
    switchToServerConsoleTab();
    emit serverButtonClicked();
}

void Launcher::PlayPage::slotReloadServerSettings()
{
    loadServerSettings();
}

void Launcher::PlayPage::slotSaveServerSettings()
{
    saveServerSettings();
}

void Launcher::PlayPage::slotApplyFormToRawConfig()
{
    serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));
    setServerSettingsStatus(tr("Form applied to raw config"));
    serverSettingsModeTabs->setCurrentWidget(rawServerSettingsTab);
}

void Launcher::PlayPage::slotSyncFormFromRawConfig()
{
    populateFormFromConfig(serverSettingsEditor->toPlainText());
    setServerSettingsStatus(tr("Form updated from raw config"));
    serverSettingsModeTabs->setCurrentWidget(formServerSettingsTab);
}
