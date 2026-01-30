#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QColor>
#include <QSettings>
#include <QWidget>

#include "tcpclient.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class LoginWindow;
}
QT_END_NAMESPACE

class ChatWindow;

class LoginWindow : public QWidget {
    Q_OBJECT

public:
    explicit LoginWindow(QWidget *parent = nullptr);
    ~LoginWindow();

private slots:
    void onConnectClicked();
    void onConnected();
    void onConnectError(const QString &error);
    void onDisconnected();
    void onLoginResponse(bool success, const QString &message);

private:
    void setupUI();
    void setupConnections();
    void saveConfig();
    void loadConfig();
    QString generateClientId();
    void updateStatus(const QString &status, const QColor &color);

private:
    Ui::LoginWindow *ui;
    TcpClient *tcpClient_;
    QSettings *settings_;
    ChatWindow *chatWindow_;
};

#endif // LOGINWINDOW_H
