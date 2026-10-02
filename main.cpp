// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QScrollArea>
#include <QSessionManager>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <array>
#include <algorithm>
#include <vector>

class DragHandle final : public QLabel
{
public:
    explicit DragHandle(QWidget *parent = nullptr)
        : QLabel(QStringLiteral("⠿"), parent)
    {
        setObjectName(QStringLiteral("dragHandle"));
        setAlignment(Qt::AlignCenter);
        setToolTip(QStringLiteral("Arraste para mover esta janela"));
        setCursor(Qt::SizeAllCursor);
        setFixedSize(34, 34);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && window()->windowHandle() && window()->windowHandle()->startSystemMove()) {
            event->accept();
            return;
        }
        QLabel::mousePressEvent(event);
    }
};

class VideoAdjustWindow final : public QWidget
{
public:
    VideoAdjustWindow()
    {
        setWindowTitle(QStringLiteral("Ajuste de vídeo"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("preferences-desktop-display")));
        setMinimumWidth(560);
        resize(570, 610);
        setMinimumSize(440, 470);
        setObjectName(QStringLiteral("window"));
        buildUi();
        m_backend = currentDesktop();
        readConfig();
        m_savedSettings = settingsObject();
        recoverStalePreview();
        configureBackendControls();
        m_timer.setSingleShot(true);
        m_timer.setInterval(110);
        connect(&m_timer, &QTimer::timeout, this, [this] { apply(); });
        m_previewHeartbeat.setInterval(1000);
        connect(&m_previewHeartbeat, &QTimer::timeout, this, [this] {
            if (!hasUnsavedChanges()) return;
            if (m_backend == QStringLiteral("kde")) refreshKWinPreviewTimestamp();
            else writePreviewConfig();
        });
        m_previewHeartbeat.start();
    }

private:
    struct Control {
        QString key;
        QString name;
        QString hint;
        int low;
        int high;
        int value;
        QString suffix;
        QSlider *slider = nullptr;
        QLabel *valueLabel = nullptr;
    };

    void buildUi()
    {
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(28, 24, 28, 24);
        outer->setSpacing(16);

        auto *header = new QHBoxLayout();
        auto *mark = new QLabel(QStringLiteral("◉"));
        mark->setObjectName(QStringLiteral("mark"));
        mark->setAlignment(Qt::AlignCenter);
        mark->setFixedSize(48, 48);
        auto *titles = new QVBoxLayout();
        auto *title = new QLabel(QStringLiteral("Ajuste de vídeo"));
        title->setObjectName(QStringLiteral("title"));
        auto *subtitle = new QLabel(QStringLiteral("Ajustes globais de imagem para Linux"));
        subtitle->setObjectName(QStringLiteral("subtitle"));
        titles->addWidget(title);
        titles->addWidget(subtitle);
        header->addWidget(mark);
        header->addLayout(titles, 1);
        header->addWidget(new DragHandle(this));
        outer->addLayout(header);

        auto *toggleCard = card(outer);
        auto *toggleRow = new QHBoxLayout(toggleCard);
        toggleRow->setContentsMargins(18, 14, 18, 14);
        m_enabled = new QCheckBox(QStringLiteral("Ativar ajustes globais"));
        m_enabled->setObjectName(QStringLiteral("enabled"));
        auto *live = new QLabel(QStringLiteral("AO VIVO"));
        live->setObjectName(QStringLiteral("live"));
        toggleRow->addWidget(m_enabled);
        toggleRow->addStretch();
        toggleRow->addWidget(live);
        connect(m_enabled, &QCheckBox::toggled, this, [this] { scheduleApply(); });

        auto *scroll = new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("controlsScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        auto *controlsCard = new QFrame(scroll);
        controlsCard->setObjectName(QStringLiteral("card"));
        scroll->setWidget(controlsCard);
        outer->addWidget(scroll, 1);
        auto *controls = new QVBoxLayout(controlsCard);
        controls->setContentsMargins(18, 16, 18, 14);
        controls->setSpacing(2);
        const std::array<Control, 6> definitions{{
            {QStringLiteral("Brightness"), QStringLiteral("Brilho"), QStringLiteral("Ajusta a intensidade geral da imagem."), -200, 200, 0, QStringLiteral("%")},
            {QStringLiteral("Contrast"), QStringLiteral("Contraste"), QStringLiteral("Expande ou comprime a diferença entre claros e escuros."), 800, 1200, 1000, QStringLiteral("%")},
            {QStringLiteral("Gamma"), QStringLiteral("Gamma"), QStringLiteral("Ajusta os meios-tons sem alterar o ponto branco."), 800, 1200, 1000, QString()},
            {QStringLiteral("Saturation"), QStringLiteral("Saturação"), QStringLiteral("Controla a intensidade das cores."), 750, 1250, 1000, QStringLiteral("%")},
            {QStringLiteral("Hue"), QStringLiteral("Matiz"), QStringLiteral("Gira as cores no círculo cromático."), -300, 300, 0, QStringLiteral("°")},
            {QStringLiteral("ColorTemperature"), QStringLiteral("Temperatura"), QStringLiteral("Move o balanço de cores entre frio e quente."), -250, 250, 0, QStringLiteral("%")},
        }};
        m_controls.reserve(definitions.size());
        for (const Control &definition : definitions) {
            m_controls.push_back(definition);
            addControl(controls, m_controls.back());
        }

        auto *presetsRow = new QHBoxLayout();
        auto *presetLabel = new QLabel(QStringLiteral("Predefinição"));
        presetLabel->setObjectName(QStringLiteral("fieldLabel"));
        m_presets = new QComboBox();
        m_presets->addItem(QStringLiteral("Personalizado"), QStringLiteral("custom"));
        m_presets->addItem(QStringLiteral("Padrão"), QStringLiteral("default"));
        m_presets->addItem(QStringLiteral("Vivo"), QStringLiteral("vivid"));
        m_presets->addItem(QStringLiteral("Cinema"), QStringLiteral("cinema"));
        m_presets->addItem(QStringLiteral("Quente"), QStringLiteral("warm"));
        presetsRow->addWidget(presetLabel);
        presetsRow->addStretch();
        presetsRow->addWidget(m_presets);
        controls->addSpacing(12);
        controls->addLayout(presetsRow);
        connect(m_presets, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index > 0) applyPreset(m_presets->itemData(index).toString());
        });

        auto *footer = new QVBoxLayout();
        m_status = new QLabel();
        m_status->setObjectName(QStringLiteral("status"));
        m_status->setWordWrap(true);
        auto *reset = new QPushButton(QStringLiteral("Restaurar padrão"));
        reset->setObjectName(QStringLiteral("reset"));
        m_installBackendButton = new QPushButton(QStringLiteral("Instalar suporte"));
        m_installBackendButton->setObjectName(QStringLiteral("installBackend"));
        auto *footerActions = new QHBoxLayout();
        footerActions->addWidget(m_installBackendButton);
        footerActions->addWidget(reset);
        footerActions->addStretch();
        auto *saveActions = new QHBoxLayout();
        saveActions->addStretch();
        m_discardButton = new QPushButton(QStringLiteral("Descartar prévia"));
        m_discardButton->setObjectName(QStringLiteral("discard"));
        m_discardButton->setEnabled(false);
        m_saveButton = new QPushButton(QStringLiteral("Salvar ajustes"));
        m_saveButton->setObjectName(QStringLiteral("save"));
        m_saveButton->setEnabled(false);
        saveActions->addWidget(m_discardButton);
        saveActions->addWidget(m_saveButton);
        footer->addWidget(m_status);
        footer->addLayout(footerActions);
        footer->addLayout(saveActions);
        outer->addLayout(footer);
        connect(reset, &QPushButton::clicked, this, [this] { applyPreset(QStringLiteral("default")); });
        connect(m_installBackendButton, &QPushButton::clicked, this, [this] { installBackendFiles(); });
        connect(m_discardButton, &QPushButton::clicked, this, [this] { discardChanges(); });
        connect(m_saveButton, &QPushButton::clicked, this, [this] { saveChanges(); });

        setStyleSheet(QStringLiteral(R"(
            QWidget#window { background: #10151c; color: #edf2f7; font-family: "Noto Sans", sans-serif; }
            QLabel#mark { color: #0ee3ba; background: #122a2c; border: 1px solid #1b4546; border-radius: 14px; font-size: 28px; }
            QLabel#title { font-size: 23px; font-weight: 700; color: #f4f7fa; }
            QLabel#subtitle, QLabel#footnote { color: #8b9aa9; font-size: 11px; }
            QLabel#dragHandle { color: #91a1af; background: #202a35; border: 1px solid #364453; border-radius: 9px; font-size: 19px; }
            QLabel#dragHandle:hover { color: #0ee3ba; border-color: #0fc5a3; }
            QScrollArea#controlsScroll { background: transparent; }
            QFrame#card { background: #171e27; border: 1px solid #26323f; border-radius: 14px; }
            QCheckBox#enabled { font-size: 14px; font-weight: 600; spacing: 10px; }
            QCheckBox::indicator { width: 19px; height: 19px; }
            QLabel#live { color: #0ee3ba; font-size: 10px; font-weight: 800; letter-spacing: 1px; }
            QLabel#fieldLabel { color: #aab8c5; font-size: 12px; }
            QLabel#controlName { color: #edf2f7; font-weight: 600; font-size: 13px; }
            QLabel#hint { color: #8998a7; font-size: 10px; }
            QLabel#value { color: #0ee3ba; background: #102b2a; border-radius: 7px; padding: 4px 8px; font-size: 11px; font-weight: 700; min-width: 50px; }
            QSlider::groove:horizontal { height: 5px; background: #303b47; border-radius: 2px; }
            QSlider::sub-page:horizontal { background: #0fc5a3; border-radius: 2px; }
            QSlider::handle:horizontal { background: #e9fffa; border: 2px solid #0fc5a3; width: 13px; height: 13px; margin: -5px 0; border-radius: 7px; }
            QComboBox { background: #202a35; border: 1px solid #364453; padding: 8px 12px; border-radius: 8px; min-width: 150px; }
            QPushButton { background: #202a35; border: 1px solid #364453; padding: 8px 10px; border-radius: 8px; color: #dce5ec; }
            QPushButton:hover { background: #293744; border-color: #0fc5a3; }
            QPushButton#save { background: #0b826f; border-color: #0fc5a3; color: #ffffff; font-weight: 700; }
            QPushButton#save:disabled, QPushButton#discard:disabled { color: #62707e; border-color: #29333e; }
            QLabel#status { color: #6fd8bd; font-size: 11px; }
        )"));
    }

    QFrame *card(QVBoxLayout *parent)
    {
        auto *frame = new QFrame(this);
        frame->setObjectName(QStringLiteral("card"));
        parent->addWidget(frame);
        return frame;
    }

    void addControl(QVBoxLayout *layout, Control &control)
    {
        auto *row = new QGridLayout();
        row->setContentsMargins(0, 8, 0, 8);
        row->setHorizontalSpacing(12);
        row->setVerticalSpacing(3);
        auto *name = new QLabel(control.name);
        name->setObjectName(QStringLiteral("controlName"));
        auto *hint = new QLabel(control.hint);
        hint->setObjectName(QStringLiteral("hint"));
        control.valueLabel = new QLabel();
        control.valueLabel->setObjectName(QStringLiteral("value"));
        control.valueLabel->setAlignment(Qt::AlignCenter);
        control.slider = new QSlider(Qt::Horizontal);
        control.slider->setRange(control.low, control.high);
        control.slider->setSingleStep(1);
        control.slider->setPageStep(10);
        control.slider->setValue(control.value);
        row->addWidget(name, 0, 0);
        row->addWidget(control.valueLabel, 0, 1, Qt::AlignRight);
        row->addWidget(hint, 1, 0, 1, 2);
        row->addWidget(control.slider, 2, 0, 1, 2);
        layout->addLayout(row);
        connect(control.slider, &QSlider::valueChanged, this, [this, &control](int value) {
            control.value = value;
            control.valueLabel->setText(displayValue(control, value));
            m_presets->setCurrentIndex(0);
            if (!m_enabled->isChecked()) m_enabled->setChecked(true);
            scheduleApply();
        });
        control.valueLabel->setText(displayValue(control, control.value));
    }

    QString displayValue(const Control &control, int value) const
    {
        if (control.key == QStringLiteral("Gamma")) return QString::number(value / 1000.0, 'f', 2);
        return QString::number(value / 10.0, 'f', 1) + control.suffix;
    }

    QString currentDesktop() const
    {
        const QString env = (qEnvironmentVariable("XDG_CURRENT_DESKTOP") + QLatin1Char(':')
                             + qEnvironmentVariable("DESKTOP_SESSION")).toLower();
        if (env.contains(QStringLiteral("kde")) || env.contains(QStringLiteral("plasma"))) return QStringLiteral("kde");
        if (env.contains(QStringLiteral("gnome"))) return QStringLiteral("gnome");
        if (env.contains(QStringLiteral("cinnamon"))) return QStringLiteral("cinnamon");
        if (env.contains(QStringLiteral("xfce"))) return QStringLiteral("xfce");
        return QStringLiteral("other");
    }

    bool waylandSession() const
    {
        return qEnvironmentVariable("XDG_SESSION_TYPE").compare(QStringLiteral("wayland"), Qt::CaseInsensitive) == 0
            || !qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty();
    }

    QString sharedConfigPath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
            + QStringLiteral("/ajuste-video/settings.json");
    }

    QJsonObject settingsObject() const
    {
        QJsonObject json;
        json.insert(QStringLiteral("enabled"), m_enabled->isChecked());
        json.insert(QStringLiteral("brightness"), m_controls[0].value / 1000.0);
        json.insert(QStringLiteral("contrast"), m_controls[1].value / 1000.0);
        json.insert(QStringLiteral("gamma"), m_controls[2].value / 1000.0);
        json.insert(QStringLiteral("saturation"), m_controls[3].value / 1000.0);
        json.insert(QStringLiteral("hue"), m_controls[4].value / 10.0);
        json.insert(QStringLiteral("temperature"), m_controls[5].value / 1000.0);
        return json;
    }

    bool saveSharedConfig() const
    {
        const QString path = sharedConfigPath();
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(QJsonDocument(settingsObject()).toJson(QJsonDocument::Compact));
        return file.commit();
    }

    QString previewConfigPath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
            + QStringLiteral("/ajuste-video/preview.json");
    }

    bool writePreviewConfig() const
    {
        const QString path = previewConfigPath();
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QJsonObject preview = settingsObject();
        preview.insert(QStringLiteral("updatedAtMs"), QDateTime::currentMSecsSinceEpoch());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(QJsonDocument(preview).toJson(QJsonDocument::Compact));
        return file.commit();
    }

    void removePreviewConfig() const
    {
        QFile::remove(previewConfigPath());
    }

    void writeKWinSettings(QSettings &config, const QString &prefix, const QJsonObject &settings) const
    {
        config.beginGroup(QStringLiteral("Effect-ajustevideo"));
        config.setValue(prefix + QStringLiteral("Enabled"), settings.value(QStringLiteral("enabled")).toBool());
        const std::array<QString, 6> jsonKeys{{QStringLiteral("brightness"), QStringLiteral("contrast"),
            QStringLiteral("gamma"), QStringLiteral("saturation"), QStringLiteral("hue"), QStringLiteral("temperature")}};
        const std::array<QString, 6> configKeys{{QStringLiteral("Brightness"), QStringLiteral("Contrast"),
            QStringLiteral("Gamma"), QStringLiteral("Saturation"), QStringLiteral("Hue"), QStringLiteral("ColorTemperature")}};
        for (size_t i = 0; i < jsonKeys.size(); ++i) {
            config.setValue(prefix + configKeys[i], settings.value(jsonKeys[i]).toDouble());
        }
        config.endGroup();
    }

    bool applyKWinPreview()
    {
        QSettings config(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                             + QStringLiteral("/kwinrc"), QSettings::IniFormat);
        writeKWinSettings(config, QStringLiteral("Preview"), settingsObject());
        config.beginGroup(QStringLiteral("Effect-ajustevideo"));
        config.setValue(QStringLiteral("PreviewActive"), true);
        config.setValue(QStringLiteral("PreviewTimestamp"), QDateTime::currentMSecsSinceEpoch());
        config.endGroup();
        config.sync();
        return config.status() == QSettings::NoError;
    }

    void refreshKWinPreviewTimestamp()
    {
        QSettings config(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                             + QStringLiteral("/kwinrc"), QSettings::IniFormat);
        config.beginGroup(QStringLiteral("Effect-ajustevideo"));
        if (config.value(QStringLiteral("PreviewActive"), false).toBool())
            config.setValue(QStringLiteral("PreviewTimestamp"), QDateTime::currentMSecsSinceEpoch());
        config.endGroup();
        config.sync();
    }

    bool reconfigureKWinEffect() const
    {
        if (!kwinEffectLoaded()) return false;
        QDBusMessage message = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
            QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("reconfigureEffect"));
        message << QStringLiteral("ajustevideo");
        const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 500);
        return reply.type() != QDBusMessage::ErrorMessage;
    }

    void clearPreview(bool updateKWin) const
    {
        removePreviewConfig();
        QSettings config(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                             + QStringLiteral("/kwinrc"), QSettings::IniFormat);
        config.beginGroup(QStringLiteral("Effect-ajustevideo"));
        config.setValue(QStringLiteral("PreviewActive"), false);
        config.setValue(QStringLiteral("PreviewTimestamp"), 0);
        config.endGroup();
        config.beginGroup(QStringLiteral("Effect-ajustevideo-app"));
        config.setValue(QStringLiteral("PreviewActive"), false);
        config.setValue(QStringLiteral("PreviewTimestamp"), 0);
        config.endGroup();
        config.sync();
        if (updateKWin && m_backend == QStringLiteral("kde")) reconfigureKWinEffect();
    }

    void recoverStalePreview()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QFile previewFile(previewConfigPath());
        if (previewFile.open(QIODevice::ReadOnly)) {
            const QJsonObject preview = QJsonDocument::fromJson(previewFile.readAll()).object();
            const qint64 updated = preview.value(QStringLiteral("updatedAtMs")).toVariant().toLongLong();
            if (updated <= 0 || now - updated > 3000) removePreviewConfig();
        }
        if (m_backend != QStringLiteral("kde")) return;

        QSettings config(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                             + QStringLiteral("/kwinrc"), QSettings::IniFormat);
        config.beginGroup(QStringLiteral("Effect-ajustevideo"));
        const bool active = config.value(QStringLiteral("PreviewActive"), false).toBool();
        const qint64 updated = config.value(QStringLiteral("PreviewTimestamp"), 0).toLongLong();
        if (active && (updated <= 0 || now - updated > 2000)) {
            config.setValue(QStringLiteral("PreviewActive"), false);
            config.setValue(QStringLiteral("PreviewTimestamp"), 0);
            config.endGroup();
            config.sync();
            reconfigureKWinEffect();
            return;
        }
        config.endGroup();
    }

    bool hasUnsavedChanges() const
    {
        return settingsObject() != m_savedSettings;
    }

    void updateActionButtons()
    {
        const bool dirty = hasUnsavedChanges();
        m_saveButton->setEnabled(dirty);
        m_discardButton->setEnabled(dirty);
    }

    void restoreSavedSettings()
    {
        const QSignalBlocker enabledBlocker(m_enabled);
        m_enabled->setChecked(m_savedSettings.value(QStringLiteral("enabled")).toBool());
        for (Control &control : m_controls) {
            const QString key = control.key == QStringLiteral("ColorTemperature")
                ? QStringLiteral("temperature") : control.key.toLower();
            const double factor = control.key == QStringLiteral("Hue") ? 10.0 : 1000.0;
            control.value = qRound(m_savedSettings.value(key).toDouble() * factor);
            control.value = std::clamp(control.value, control.low, control.high);
            const QSignalBlocker sliderBlocker(control.slider);
            control.slider->setValue(control.value);
            control.valueLabel->setText(displayValue(control, control.value));
        }
        m_presets->setCurrentIndex(0);
    }

    void discardChanges()
    {
        m_timer.stop();
        restoreSavedSettings();
        clearPreview(true);
        if (m_backend == QStringLiteral("xfce") && !waylandSession()) applyXrandr();
        updateActionButtons();
        m_status->setText(QStringLiteral("Prévia descartada · último perfil salvo restaurado"));
    }

    void saveChanges()
    {
        if (!hasUnsavedChanges()) return;
        QMessageBox confirmation(QMessageBox::Question, QStringLiteral("Salvar ajustes"),
            QStringLiteral("Salvar estes valores como seu perfil permanente?"),
            QMessageBox::Save | QMessageBox::Cancel, this);
        confirmation.button(QMessageBox::Save)->setText(QStringLiteral("Salvar"));
        confirmation.button(QMessageBox::Cancel)->setText(QStringLiteral("Continuar editando"));
        if (confirmation.exec() != QMessageBox::Save) return;

        const QJsonObject current = settingsObject();
        if (!saveSharedConfig()) {
            m_status->setText(QStringLiteral("Não consegui salvar os ajustes"));
            return;
        }
        if (m_backend == QStringLiteral("kde")) {
            QSettings config(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                                 + QStringLiteral("/kwinrc"), QSettings::IniFormat);
            writeKWinSettings(config, QString(), current);
            config.beginGroup(QStringLiteral("Plugins"));
            config.setValue(QStringLiteral("ajustevideoEnabled"), true);
            config.setValue(QStringLiteral("ajustevideo-appEnabled"), false);
            config.endGroup();
            config.sync();
        }
        m_savedSettings = current;
        clearPreview(m_backend == QStringLiteral("kde"));
        updateActionButtons();
        m_status->setText(QStringLiteral("Ajustes salvos e confirmados"));
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (hasUnsavedChanges()) discardChanges();
        QWidget::closeEvent(event);
    }

private:

    bool platformBackendInstalled() const
    {
        const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        if (m_backend == QStringLiteral("gnome"))
            return QFileInfo::exists(data + QStringLiteral("/gnome-shell/extensions/ajuste-video@xoykor/extension.js"));
        if (m_backend == QStringLiteral("cinnamon"))
            return QFileInfo::exists(data + QStringLiteral("/cinnamon/extensions/ajuste-video@xoykor/extension.js"));
        if (m_backend == QStringLiteral("kde")) {
            if (QFileInfo::exists(QStringLiteral("/usr/lib/qt6/plugins/kwin/effects/plugins/ajustevideo.so")))
                return true;
            QDBusMessage query = QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("listOfEffects"));
            const QDBusMessage reply = QDBusConnection::sessionBus().call(query, QDBus::Block, 1000);
            if (reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty()) {
                const QStringList effectsList = reply.arguments().constFirst().toStringList();
                if (effectsList.contains(QStringLiteral("ajustevideo"))) return true;
            }
            return false;
        }
        return false;
    }

    void configureBackendControls()
    {
        m_backend = currentDesktop();
        const bool xfceX11 = m_backend == QStringLiteral("xfce") && !waylandSession();
        for (Control &control : m_controls) {
            const bool supported = m_backend != QStringLiteral("xfce")
                || (xfceX11 && (control.key == QStringLiteral("Brightness") || control.key == QStringLiteral("Gamma")));
            control.slider->setEnabled(supported);
            control.valueLabel->setEnabled(supported);
        }
        const bool hasInstaller = m_backend == QStringLiteral("gnome") || m_backend == QStringLiteral("cinnamon");
        m_installBackendButton->setText(QStringLiteral("Instalar suporte"));
        m_installBackendButton->setVisible(hasInstaller && !platformBackendInstalled());
        if (m_backend == QStringLiteral("kde")) {
            m_status->setText(!platformBackendInstalled()
                ? QStringLiteral("Suporte KWin ausente · execute o instalador do projeto")
                : kwinEffectLoaded()
                    ? QStringLiteral("KDE · efeito ativo")
                    : QStringLiteral("KDE · o efeito será carregado ao iniciar a prévia"));
        } else if (m_backend == QStringLiteral("gnome") || m_backend == QStringLiteral("cinnamon")) {
            m_status->setText(platformBackendInstalled() ? QStringLiteral("Backend instalado · ajustes ao vivo")
                                                         : QStringLiteral("Instale o backend para ativar os ajustes"));
        } else if (m_backend == QStringLiteral("xfce")) {
            m_status->setText(xfceX11 ? QStringLiteral("XFCE/X11 · brilho e gamma disponíveis")
                                      : QStringLiteral("XFCE/Wayland · compositor atual sem backend de cor"));
        } else {
            m_status->setText(QStringLiteral("Ambiente sem backend de vídeo disponível"));
            for (Control &control : m_controls) {
                control.slider->setEnabled(false);
                control.valueLabel->setEnabled(false);
            }
        }
    }

    QString bundledDataDir() const
    {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/../share/ajuste-video");
    }

    bool kwinEffectLoaded() const
    {
        QDBusMessage query = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
            QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("isEffectLoaded"));
        query << QStringLiteral("ajustevideo");
        const QDBusMessage reply = QDBusConnection::sessionBus().call(query, QDBus::Block, 1000);
        return reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty()
            && reply.arguments().constFirst().toBool();
    }

    bool ensureKWinEffectLoaded() const
    {
        if (kwinEffectLoaded()) return true;
        if (!platformBackendInstalled()) return false;
        QDBusMessage load = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
            QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("loadEffect"));
        load << QStringLiteral("ajustevideo");
        const QDBusMessage reply = QDBusConnection::sessionBus().call(load, QDBus::Block, 3000);
        return reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty()
            && reply.arguments().constFirst().toBool() && kwinEffectLoaded();
    }

    void installBackendFiles()
    {
        const QString id = QStringLiteral("ajuste-video@xoykor");
        const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const bool gnome = m_backend == QStringLiteral("gnome");
        const QString source = bundledDataDir() + QStringLiteral("/backends/") + (gnome ? QStringLiteral("gnome") : QStringLiteral("cinnamon"));
        const QString destination = data + (gnome ? QStringLiteral("/gnome-shell/extensions/") : QStringLiteral("/cinnamon/extensions/")) + id;
        const QStringList files{QStringLiteral("extension.js"), QStringLiteral("metadata.json")};
        if (!QDir().mkpath(destination)) {
            m_status->setText(QStringLiteral("Não consegui criar a pasta do backend"));
            return;
        }
        for (const QString &name : files) {
            const QString from = source + QLatin1Char('/') + name;
            const QString to = destination + QLatin1Char('/') + name;
            if (!QFileInfo::exists(from) || (QFile::exists(to) && !QFile::remove(to)) || !QFile::copy(from, to)) {
                m_status->setText(QStringLiteral("Falha ao instalar o backend do ambiente"));
                return;
            }
        }
        if (gnome) {
            QProcess::startDetached(QStringLiteral("gnome-extensions"), {QStringLiteral("enable"), id});
            m_status->setText(QStringLiteral("Instalado · encerre e reabra a sessão GNOME"));
        } else {
            QProcess get;
            get.start(QStringLiteral("gsettings"), {QStringLiteral("get"), QStringLiteral("org.cinnamon"), QStringLiteral("enabled-extensions")});
            QStringList extensions;
            if (get.waitForFinished(1000)) {
                const QRegularExpression quoted(QStringLiteral("'([^']+)'"));
                auto it = quoted.globalMatch(QString::fromLocal8Bit(get.readAllStandardOutput()));
                while (it.hasNext()) extensions.push_back(it.next().captured(1));
            }
            if (!extensions.contains(id)) extensions.push_back(id);
            QStringList serialized;
            for (const QString &ext : extensions) serialized.push_back(QLatin1Char('\'') + ext + QLatin1Char('\''));
            const QString value = QLatin1Char('[') + serialized.join(QStringLiteral(", ")) + QLatin1Char(']');
            QProcess::startDetached(QStringLiteral("gsettings"), {QStringLiteral("set"), QStringLiteral("org.cinnamon"), QStringLiteral("enabled-extensions"), value});
            m_status->setText(QStringLiteral("Instalado · encerre e reabra a sessão Cinnamon"));
        }
        m_installBackendButton->hide();
    }

    void readConfig()
    {
        QFile shared(sharedConfigPath());
        QJsonObject json;
        if (shared.open(QIODevice::ReadOnly)) json = QJsonDocument::fromJson(shared.readAll()).object();
        QSettings kwinConfig(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                                 + QStringLiteral("/kwinrc"), QSettings::IniFormat);
        const QString effectGroup = QStringLiteral("Effect-ajustevideo");
        const QString legacyGroup = QStringLiteral("Effect-ajustevideo-app");
        const auto savedValue = [&kwinConfig, &effectGroup, &legacyGroup](const QString &key, const QVariant &fallback) {
            const QString currentKey = effectGroup + QLatin1Char('/') + key;
            if (kwinConfig.contains(currentKey)) return kwinConfig.value(currentKey);
            return kwinConfig.value(legacyGroup + QLatin1Char('/') + key, fallback);
        };
        const QSignalBlocker enabledBlocker(m_enabled);
        m_enabled->setChecked(json.value(QStringLiteral("enabled")).toBool(savedValue(QStringLiteral("Enabled"), false).toBool()));
        for (Control &control : m_controls) {
            const double neutral = control.key == QStringLiteral("Contrast") || control.key == QStringLiteral("Gamma") || control.key == QStringLiteral("Saturation") ? 1.0 : 0.0;
            const double factor = control.key == QStringLiteral("Hue") ? 10.0 : 1000.0;
            const QString jsonKey = control.key == QStringLiteral("ColorTemperature") ? QStringLiteral("temperature") : control.key.toLower();
            const double stored = json.contains(jsonKey) ? json.value(jsonKey).toDouble()
                                                         : savedValue(control.key, neutral).toDouble();
            control.value = qRound(stored * factor);
            control.value = std::clamp(control.value, control.low, control.high);
            const QSignalBlocker sliderBlocker(control.slider);
            control.slider->setValue(control.value);
            control.valueLabel->setText(displayValue(control, control.value));
        }
        m_status->setText(QStringLiteral("KWin pronto · os ajustes são aplicados enquanto você move os controles"));
    }

    void applyPreset(const QString &name)
    {
        std::array<int, 6> values{0, 1000, 1000, 1000, 0, 0};
        if (name == QStringLiteral("vivid")) values = {0, 1050, 1000, 1250, 0, 0};
        else if (name == QStringLiteral("cinema")) values = {-30, 1080, 950, 1120, 0, 0};
        else if (name == QStringLiteral("warm")) values = {0, 1000, 1000, 1050, 0, 200};
        m_timer.stop();
        for (size_t i = 0; i < m_controls.size(); ++i) m_controls[i].slider->setValue(values[i]);
        if (name == QStringLiteral("default")) m_enabled->setChecked(false);
        else m_enabled->setChecked(true);
        m_timer.stop();
        apply();
    }

    void scheduleApply()
    {
        updateActionButtons();
        m_timer.start();
    }

    void apply()
    {
        const bool kwin = m_backend == QStringLiteral("kde");
        const bool effectLoaded = kwin && ensureKWinEffectLoaded();
        if (kwin && !effectLoaded) {
            updateActionButtons();
            m_status->setText(QStringLiteral("Falha ao carregar o efeito KWin · execute novamente o instalador"));
            return;
        }

        if (!hasUnsavedChanges()) {
            clearPreview(kwin);
            if (m_backend == QStringLiteral("xfce") && !waylandSession()) applyXrandr();
            else m_status->setText(QStringLiteral("Último perfil salvo restaurado"));
            updateActionButtons();
            return;
        }

        if (m_backend == QStringLiteral("xfce")) {
            if (waylandSession()) {
                m_status->setText(QStringLiteral("XFCE/Wayland · backend de cor indisponível neste compositor"));
                updateActionButtons();
                return;
            }
            applyXrandr();
            if (hasUnsavedChanges()) m_status->setText(QStringLiteral("Prévia ao vivo · ainda não salva"));
            updateActionButtons();
            return;
        }

        if (m_backend == QStringLiteral("gnome") || m_backend == QStringLiteral("cinnamon")) {
            if (!platformBackendInstalled()) {
                m_status->setText(QStringLiteral("Instale o suporte do ambiente para ativar a prévia"));
                updateActionButtons();
                return;
            }
            if (!writePreviewConfig()) {
                m_status->setText(QStringLiteral("Não consegui criar o arquivo temporário da prévia"));
                updateActionButtons();
                return;
            }
            m_status->setText(QStringLiteral("Prévia ao vivo · ainda não salva"));
            updateActionButtons();
            return;
        }

        if (kwin) {
            if (!applyKWinPreview()) {
                m_status->setText(QStringLiteral("Não consegui preparar a prévia temporária no KWin"));
                updateActionButtons();
                return;
            }
            if (!reconfigureKWinEffect()) {
                m_status->setText(QStringLiteral("KWin não aplicou a prévia · confira se o efeito continua carregado"));
                updateActionButtons();
                return;
            }
            m_status->setText(QStringLiteral("Prévia ao vivo · ainda não salva"));
            updateActionButtons();
            return;
        }
        updateActionButtons();
    }

    void applyXrandr()
    {
        QProcess query;
        query.start(QStringLiteral("xrandr"), {QStringLiteral("--query")});
        if (!query.waitForFinished(1500) || query.exitCode() != 0) {
            m_status->setText(QStringLiteral("Não consegui consultar as saídas do X11"));
            return;
        }
        const QString output = QString::fromLocal8Bit(query.readAllStandardOutput());
        const QRegularExpression connected(QStringLiteral("^([^\\s]+) connected"), QRegularExpression::MultilineOption);
        QStringList displays;
        auto matches = connected.globalMatch(output);
        while (matches.hasNext()) displays.push_back(matches.next().captured(1));
        if (displays.isEmpty()) {
            m_status->setText(QStringLiteral("Nenhuma tela conectada foi encontrada"));
            return;
        }
        const double brightness = 1.0 + (m_enabled->isChecked() ? m_controls[0].value / 1000.0 : 0.0);
        const double gamma = m_enabled->isChecked() ? m_controls[2].value / 1000.0 : 1.0;
        const QString level = QString::number(brightness, 'f', 2);
        const QString gammaLevel = QString::number(gamma, 'f', 2);
        for (const QString &display : displays) {
            QProcess set;
            set.start(QStringLiteral("xrandr"), {QStringLiteral("--output"), display,
                     QStringLiteral("--brightness"), level, QStringLiteral("--gamma"),
                     gammaLevel + QLatin1Char(':') + gammaLevel + QLatin1Char(':') + gammaLevel});
            if (!set.waitForFinished(1500) || set.exitCode() != 0) {
                m_status->setText(QStringLiteral("Falha ao ajustar a tela %1").arg(display));
                return;
            }
        }
        m_status->setText(m_enabled->isChecked()
                              ? QStringLiteral("XFCE/X11 · brilho e gamma aplicados (temporário)")
                              : QStringLiteral("XFCE/X11 · ajustes restaurados"));
    }

    QCheckBox *m_enabled = nullptr;
    QComboBox *m_presets = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_installBackendButton = nullptr;
    QPushButton *m_discardButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QString m_backend;
    QJsonObject m_savedSettings;
    QTimer m_timer;
    QTimer m_previewHeartbeat;
    std::vector<Control> m_controls;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ajuste-video"));
    QApplication::setOrganizationName(QStringLiteral("xoykor"));
    VideoAdjustWindow window;
    QObject::connect(&app, &QGuiApplication::commitDataRequest, &window,
        [&window](QSessionManager &) { window.close(); });
    window.show();
    return app.exec();
}
