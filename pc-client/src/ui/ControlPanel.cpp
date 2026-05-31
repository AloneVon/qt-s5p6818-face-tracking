#include "ui/ControlPanel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace secpc {

ControlPanel::ControlPanel(QWidget* parent) : QWidget(parent) {
    // ---- Connection ------------------------------------------------------
    auto* connBox = new QGroupBox(tr("Connection"), this);
    host_ = new QLineEdit(QStringLiteral("127.0.0.1"), connBox);
    port_ = new QSpinBox(connBox);
    port_->setRange(1, 65535);
    port_->setValue(8888);
    token_ = new QLineEdit(connBox);
    token_->setEchoMode(QLineEdit::Password);
    token_->setPlaceholderText(tr("if required"));
    tls_ = new QCheckBox(tr("Encrypt (TLS)"), connBox);
    certPath_ = new QLineEdit(connBox);
    certPath_->setPlaceholderText(tr("server-cert.pem to pin (optional)"));
    certPath_->setEnabled(false);  // only meaningful with TLS on
    connectBtn_ = new QPushButton(connBox);

    auto* connForm = new QFormLayout(connBox);
    connForm->addRow(tr("Host"), host_);
    connForm->addRow(tr("Port"), port_);
    connForm->addRow(tr("Token"), token_);
    connForm->addRow(tr("TLS"), tls_);
    connForm->addRow(tr("Cert"), certPath_);
    connForm->addRow(connectBtn_);

    // ---- Gimbal d-pad ----------------------------------------------------
    // Sign convention emitted to the terminal: pan+ = right, tilt+ = up.
    gimbalBox_  = new QGroupBox(tr("Gimbal"), this);
    auto* up    = new QPushButton(QStringLiteral("▲"), gimbalBox_);  // up
    auto* down  = new QPushButton(QStringLiteral("▼"), gimbalBox_);  // down
    auto* left  = new QPushButton(QStringLiteral("◀"), gimbalBox_);  // left
    auto* right = new QPushButton(QStringLiteral("▶"), gimbalBox_);  // right
    auto* home  = new QPushButton(QStringLiteral("⌂"), gimbalBox_);  // center
    for (QPushButton* b : {up, down, left, right, home}) b->setFixedSize(56, 40);

    step_ = new QDoubleSpinBox(gimbalBox_);
    step_->setRange(0.5, 30.0);
    step_->setSingleStep(0.5);
    step_->setValue(5.0);
    step_->setSuffix(QStringLiteral(" °"));  // degrees

    auto* pad = new QGridLayout;
    pad->addWidget(up,    0, 1);
    pad->addWidget(left,  1, 0);
    pad->addWidget(home,  1, 1);
    pad->addWidget(right, 1, 2);
    pad->addWidget(down,  2, 1);

    auto* stepRow = new QFormLayout;
    stepRow->addRow(tr("Step"), step_);

    auto* gimbalLayout = new QVBoxLayout(gimbalBox_);
    gimbalLayout->addLayout(pad);
    gimbalLayout->addLayout(stepRow);

    // ---- Mode ------------------------------------------------------------
    modeBox_   = new QGroupBox(tr("Mode"), this);
    autoTrack_ = new QCheckBox(tr("Auto-track faces"), modeBox_);
    autoTrack_->setChecked(true);
    auto* modeLayout = new QVBoxLayout(modeBox_);
    modeLayout->addWidget(autoTrack_);

    // ---- Assemble --------------------------------------------------------
    auto* root = new QVBoxLayout(this);
    root->addWidget(connBox);
    root->addWidget(gimbalBox_);
    root->addWidget(modeBox_);
    root->addStretch(1);

    // ---- Wiring ----------------------------------------------------------
    connect(connectBtn_, &QPushButton::clicked, this, &ControlPanel::onConnectClicked);

    connect(up,    &QPushButton::clicked, this, [this] { emit panTiltStep(0.0, +step_->value()); });
    connect(down,  &QPushButton::clicked, this, [this] { emit panTiltStep(0.0, -step_->value()); });
    connect(left,  &QPushButton::clicked, this, [this] { emit panTiltStep(-step_->value(), 0.0); });
    connect(right, &QPushButton::clicked, this, [this] { emit panTiltStep(+step_->value(), 0.0); });
    connect(home,  &QPushButton::clicked, this, [this] { emit homeRequested(); });

    connect(autoTrack_, &QCheckBox::toggled, this, &ControlPanel::modeChanged);

    connect(tls_, &QCheckBox::toggled, this, [this](bool on) {
        certPath_->setEnabled(on);
        // Convenience: hop between the known default ports, but never clobber a
        // port the user typed themselves.
        if (on && port_->value() == 8888) port_->setValue(8443);
        else if (!on && port_->value() == 8443) port_->setValue(8888);
    });

    setConnected(false);
}

void ControlPanel::onConnectClicked() {
    if (connected_)
        emit disconnectRequested();
    else
        emit connectRequested(host_->text().trimmed(),
                              static_cast<quint16>(port_->value()),
                              token_->text(),
                              tls_->isChecked(),
                              certPath_->text().trimmed());
}

void ControlPanel::setConnected(bool connected) {
    connected_ = connected;
    connectBtn_->setText(connected ? tr("Disconnect") : tr("Connect"));
    host_->setEnabled(!connected);
    port_->setEnabled(!connected);
    token_->setEnabled(!connected);
    tls_->setEnabled(!connected);
    certPath_->setEnabled(!connected && tls_->isChecked());
    gimbalBox_->setEnabled(connected);
    modeBox_->setEnabled(connected);
}

}  // namespace secpc
