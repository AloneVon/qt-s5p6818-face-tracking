// ControlPanel.h — connection fields, a pan/tilt d-pad with a step-size knob,
// and an auto/manual toggle. It only emits intent; MainWindow translates the
// signals into TcpClient calls. Gimbal/mode controls are disabled until
// connected.
#pragma once

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace secpc {

class ControlPanel : public QWidget {
    Q_OBJECT
public:
    explicit ControlPanel(QWidget* parent = nullptr);

    void setConnected(bool connected);

signals:
    void connectRequested(const QString& host, quint16 port, const QString& token,
                          bool tls, const QString& certPath);
    void disconnectRequested();
    void panTiltStep(double dPanDeg, double dTiltDeg);  // relative nudge
    void homeRequested();                               // re-center the gimbal
    void modeChanged(bool autoTrack);

private slots:
    void onConnectClicked();

private:
    QLineEdit*      host_;
    QSpinBox*       port_;
    QLineEdit*      token_;
    QCheckBox*      tls_;       // encrypt the link (connect to the TLS port)
    QLineEdit*      certPath_;  // PEM cert to pin; empty = unverified (MITM-able)
    QPushButton*    connectBtn_;
    QDoubleSpinBox* step_;
    QCheckBox*      autoTrack_;
    QGroupBox*      gimbalBox_;
    QGroupBox*      modeBox_;
    bool            connected_ = false;
};

}  // namespace secpc
