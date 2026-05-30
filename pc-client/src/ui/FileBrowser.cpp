#include "ui/FileBrowser.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QGridLayout>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

namespace secpc {

FileBrowser::FileBrowser(QWidget* parent) : QWidget(parent) {
    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);

    refreshBtn_  = new QPushButton(tr("Refresh"), this);
    snapshotBtn_ = new QPushButton(tr("Snapshot"), this);
    downloadBtn_ = new QPushButton(tr("Download"), this);
    deleteBtn_   = new QPushButton(tr("Delete"), this);

    auto* btns = new QGridLayout;
    btns->addWidget(refreshBtn_,  0, 0);
    btns->addWidget(snapshotBtn_, 0, 1);
    btns->addWidget(downloadBtn_, 1, 0);
    btns->addWidget(deleteBtn_,   1, 1);

    auto* root = new QVBoxLayout(this);
    root->addWidget(list_, 1);
    root->addLayout(btns);

    connect(refreshBtn_,  &QPushButton::clicked, this, &FileBrowser::refreshRequested);
    connect(snapshotBtn_, &QPushButton::clicked, this, &FileBrowser::snapshotRequested);
    connect(downloadBtn_, &QPushButton::clicked, this, [this] {
        const QString n = currentName();
        if (!n.isEmpty()) emit downloadRequested(n);
    });
    connect(deleteBtn_, &QPushButton::clicked, this, [this] {
        const QString n = currentName();
        if (!n.isEmpty()) emit deleteRequested(n);
    });

    setConnected(false);
}

void FileBrowser::setConnected(bool connected) {
    refreshBtn_->setEnabled(connected);
    snapshotBtn_->setEnabled(connected);
    downloadBtn_->setEnabled(connected);
    deleteBtn_->setEnabled(connected);
    list_->setEnabled(connected);
    if (!connected) list_->clear();
}

void FileBrowser::setFiles(const QVector<RemoteFile>& files) {
    const QString keep = currentName();  // preserve selection across refreshes

    list_->clear();
    for (const RemoteFile& f : files) {
        const QString size = QLocale().formattedDataSize(static_cast<qint64>(f.size));
        QString label = f.name + QStringLiteral("\t") + size;
        if (f.mtime) {
            label += QStringLiteral("  ") +
                     QDateTime::fromSecsSinceEpoch(static_cast<qint64>(f.mtime))
                         .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        }

        auto* item = new QListWidgetItem(label, list_);
        item->setData(Qt::UserRole, f.name);
        if (f.name == keep) list_->setCurrentItem(item);
    }
}

QString FileBrowser::currentName() const {
    const QListWidgetItem* item = list_->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

}  // namespace secpc
