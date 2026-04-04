/*
    playpage.cpp
    EncoreMP: Play page with server IP/Port fields.
    Replaces profile selector with direct server connection input.
    Values are saved to tes3mp-client-default.cfg on Play.
*/
#include "playpage.hpp"
#include <QListView>

Launcher::PlayPage::PlayPage(QWidget *parent) : QWidget(parent)
{
    setObjectName("PlayPage");
    setupUi(this);

    connect(playButton, SIGNAL(clicked()), this, SLOT(slotPlayClicked()));
}

void Launcher::PlayPage::setServerAddress(const QString& addr)
{
    serverAddressEdit->setText(addr);
}

void Launcher::PlayPage::setServerPort(const QString& port)
{
    serverPortEdit->setText(port);
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

void Launcher::PlayPage::slotPlayClicked()
{
    emit playButtonClicked();
}
