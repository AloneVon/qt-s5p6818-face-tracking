// FileBrowser.h — lists the terminal's recorded captures and offers
// download/delete plus a snapshot trigger. It only emits intent; MainWindow
// turns the signals into TcpClient requests and feeds back the file list.
#pragma once

#include <QWidget>

#include "net/TcpClient.h"  // secpc::RemoteFile

class QListWidget;
class QPushButton;

namespace secpc {

class FileBrowser : public QWidget {
    Q_OBJECT
public:
    explicit FileBrowser(QWidget* parent = nullptr);

    void setConnected(bool connected);

public slots:
    void setFiles(const QVector<RemoteFile>& files);

signals:
    void refreshRequested();
    void downloadRequested(const QString& name);
    void deleteRequested(const QString& name);
    void snapshotRequested();

private:
    QString currentName() const;

    QListWidget* list_;
    QPushButton* refreshBtn_;
    QPushButton* snapshotBtn_;
    QPushButton* downloadBtn_;
    QPushButton* deleteBtn_;
};

}  // namespace secpc
