// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
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
#include <QPushButton>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QScrollArea>
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
        configureBackendControls();
        m_timer.setSingleShot(true);
        m_timer.setInterval(110);
        connect(&m_timer, &QTimer::timeout, this, [this] { apply(); });
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
            {QStringLiteral("Brightness"), QStringLiteral("Brilho"), QStringLiteral("Ajusta a intensidade geral da imagem."), -20, 20, 0, QStringLiteral("%")},
            {QStringLiteral("Contrast"), QStringLiteral("Contraste"), QStringLiteral("Expande ou comprime a diferença entre claros e escuros."), 80, 120, 100, QStringLiteral("%")},
            {QStringLiteral("Gamma"), QStringLiteral("Gamma"), QStringLiteral("Ajusta os meios-tons sem alterar o ponto branco."), 80, 120, 100, QString()},
            {QStringLiteral("Saturation"), QStringLiteral("Saturação"), QStringLiteral("Controla a intensidade das cores."), 75, 125, 100, QStringLiteral("%")},
            {QStringLiteral("Hue"), QStringLiteral("Matiz"), QStringLiteral("Gira as cores no círculo cromático."), -30, 30, 0, QStringLiteral("°")},
            {QStringLiteral("ColorTemperature"), QStringLiteral("Temperatura"), QStringLiteral("Move o balanço de cores entre frio e quente."), -25, 25, 0, QStringLiteral("%")},
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
        m_installBackendButton = new QPushButton(QStringLiteral("Ativar no KWin"));
        m_installBackendButton->setObjectName(QStringLiteral("installBackend"));
        auto *footerActions = new QHBoxLayout();
        footerActions->addStretch();
        footerActions->addWidget(m_installBackendButton);
        footerActions->addWidget(reset);
        footer->addWidget(m_status);
        footer->addLayout(footerActions);
        outer->addLayout(footer);
        connect(reset, &QPushButton::clicked, this, [this] { applyPreset(QStringLiteral("default")); });
        connect(m_installBackendButton, &QPushButton::clicked, this, [this] { installBackendFiles(); });

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
            scheduleApply();
        });
        control.valueLabel->setText(displayValue(control, control.value));
    }

    QString displayValue(const Control &control, int value) const
    {
        if (control.key == QStringLiteral("Gamma")) return QString::number(value / 100.0, 'f', 2);
        return QString::number(value) + control.suffix;
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
        json.insert(QStringLiteral("brightness"), m_controls[0].value / 100.0);
        json.insert(QStringLiteral("contrast"), m_controls[1].value / 100.0);
        json.insert(QStringLiteral("gamma"), m_controls[2].value / 100.0);
        json.insert(QStringLiteral("saturation"), m_controls[3].value / 100.0);
        json.insert(QStringLiteral("hue"), m_controls[4].value);
        json.insert(QStringLiteral("temperature"), m_controls[5].value / 100.0);
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

    bool platformBackendInstalled() const
    {
        const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        if (m_backend == QStringLiteral("gnome"))
            return QFileInfo::exists(data + QStringLiteral("/gnome-shell/extensions/ajuste-video@xoykor/extension.js"));
        if (m_backend == QStringLiteral("cinnamon"))
            return QFileInfo::exists(data + QStringLiteral("/cinnamon/extensions/ajuste-video@xoykor/extension.js"));
        if (m_backend == QStringLiteral("kde"))
            return QFileInfo::exists(data + QStringLiteral("/kwin/effects/ajustevideo-app/contents/code/main.js"));
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
        const bool hasInstaller = m_backend == QStringLiteral("gnome") || m_backend == QStringLiteral("cinnamon")
            || m_backend == QStringLiteral("kde");
        m_installBackendButton->setText(m_backend == QStringLiteral("kde")
            ? QStringLiteral("Ativar no KWin") : QStringLiteral("Instalar suporte"));
        m_installBackendButton->setVisible(hasInstaller
            && (!platformBackendInstalled() || m_backend == QStringLiteral("kde")));
        if (m_backend == QStringLiteral("kde")) {
            m_installBackendButton->setText(QStringLiteral("Ativar / atualizar KWin"));
            m_status->setText(QStringLiteral("KDE · ajustes globais processados pelo KWin"));
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
        const QString appDir = qEnvironmentVariable("APPDIR");
        if (!appDir.isEmpty()) return appDir + QStringLiteral("/usr/share/ajuste-video");
        return QCoreApplication::applicationDirPath() + QStringLiteral("/../share/ajuste-video");
    }

    void installBackendFiles()
    {
        if (m_backend == QStringLiteral("kde")) {
            const QString source = bundledDataDir() + QStringLiteral("/backends/kwin");
            const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
            const QString destination = data + QStringLiteral("/kwin/effects/ajustevideo-app");
            const QStringList files{
                QStringLiteral("metadata.json"),
                QStringLiteral("contents/code/main.js"),
                QStringLiteral("contents/config/main.xml"),
                QStringLiteral("contents/shaders/adjust.frag"),
                QStringLiteral("contents/shaders/adjust_core.frag")
            };
            for (const QString &name : files) {
                const QString from = source + QLatin1Char('/') + name;
                const QString to = destination + QLatin1Char('/') + name;
                if (!QFileInfo::exists(from) || !QDir().mkpath(QFileInfo(to).absolutePath())
                    || (QFile::exists(to) && !QFile::remove(to)) || !QFile::copy(from, to)) {
                    m_status->setText(QStringLiteral("Não consegui ativar o efeito KWin pelo AppImage"));
                    m_status->setToolTip(QStringLiteral("Falha ao copiar: ") + from + QStringLiteral(" → ") + to);
                    return;
                }
            }

            QSettings kwinConfig(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                                     + QStringLiteral("/kwinrc"), QSettings::IniFormat);
            const QStringList keys{QStringLiteral("Brightness"), QStringLiteral("Contrast"), QStringLiteral("Gamma"),
                                   QStringLiteral("Saturation"), QStringLiteral("Hue"), QStringLiteral("ColorTemperature")};
            kwinConfig.beginGroup(QStringLiteral("Effect-ajustevideo-app"));
            kwinConfig.setValue(QStringLiteral("Enabled"), m_enabled->isChecked());
            for (size_t i = 0; i < m_controls.size(); ++i) {
                const Control &control = m_controls[i];
                kwinConfig.setValue(keys.at(static_cast<qsizetype>(i)),
                    control.key == QStringLiteral("Hue") ? control.value : control.value / 100.0);
            }
            kwinConfig.endGroup();
            kwinConfig.beginGroup(QStringLiteral("Plugins"));
            kwinConfig.setValue(QStringLiteral("ajustevideo-appEnabled"), true);
            kwinConfig.endGroup();
            kwinConfig.sync();

            QDBusMessage refreshKWin = QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                QStringLiteral("org.kde.KWin"), QStringLiteral("reconfigure"));
            QDBusConnection::sessionBus().call(refreshKWin, QDBus::Block, 1000);

            QDBusMessage loadEffect = QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("loadEffect"));
            loadEffect << QStringLiteral("ajustevideo-app");
            const QDBusMessage reply = QDBusConnection::sessionBus().call(loadEffect, QDBus::Block, 3000);
            if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()
                || !reply.arguments().first().toBool()) {
                m_status->setText(QStringLiteral("O KWin recusou o efeito · detalhes no tooltip"));
                m_status->setToolTip(reply.errorMessage().isEmpty()
                    ? QStringLiteral("Confira o log do KWin com: journalctl --user -b | grep -i ajustevideo")
                    : reply.errorMessage());
                return;
            }
            if (QFileInfo::exists(QStringLiteral("/usr/lib/qt6/plugins/kwin/effects/plugins/ajustevideo.so"))) {
                kwinConfig.beginGroup(QStringLiteral("Plugins"));
                kwinConfig.setValue(QStringLiteral("ajustevideoEnabled"), false);
                kwinConfig.endGroup();
                kwinConfig.beginGroup(QStringLiteral("Effect-ajustevideo"));
                kwinConfig.setValue(QStringLiteral("Enabled"), false);
                kwinConfig.endGroup();
                kwinConfig.sync();
                QDBusConnection::sessionBus().call(refreshKWin, QDBus::Block, 1000);
            }
            QDBusMessage reconfigure = QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("reconfigureEffect"));
            reconfigure << QStringLiteral("ajustevideo-app");
            QDBusConnection::sessionBus().call(reconfigure, QDBus::Block, 1000);
            m_status->setText(QStringLiteral("Ajuste de vídeo ativado no KWin pelo AppImage"));
            m_status->setToolTip(QStringLiteral("O efeito fica nos dados do seu usuário; não instalou pacotes do sistema."));
            m_installBackendButton->hide();
            return;
        }
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
        const QString effectGroup = m_backend == QStringLiteral("kde")
            ? QStringLiteral("Effect-ajustevideo-app") : QStringLiteral("Effect-ajustevideo");
        const QString legacyGroup = QStringLiteral("Effect-ajustevideo");
        const auto savedValue = [&kwinConfig, &effectGroup, &legacyGroup](const QString &key, const QVariant &fallback) {
            const QString currentKey = effectGroup + QLatin1Char('/') + key;
            if (kwinConfig.contains(currentKey)) return kwinConfig.value(currentKey);
            return kwinConfig.value(legacyGroup + QLatin1Char('/') + key, fallback);
        };
        const QSignalBlocker enabledBlocker(m_enabled);
        m_enabled->setChecked(json.value(QStringLiteral("enabled")).toBool(savedValue(QStringLiteral("Enabled"), false).toBool()));
        for (Control &control : m_controls) {
            const double neutral = control.key == QStringLiteral("Contrast") || control.key == QStringLiteral("Gamma") || control.key == QStringLiteral("Saturation") ? 1.0 : 0.0;
            const double factor = control.key == QStringLiteral("Hue") ? 1.0 : 100.0;
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
        std::array<int, 6> values{0, 100, 100, 100, 0, 0};
        if (name == QStringLiteral("vivid")) values = {0, 105, 100, 125, 0, 0};
        else if (name == QStringLiteral("cinema")) values = {-3, 108, 95, 112, 0, 0};
        else if (name == QStringLiteral("warm")) values = {0, 100, 100, 105, 0, 20};
        m_timer.stop();
        for (size_t i = 0; i < m_controls.size(); ++i) m_controls[i].slider->setValue(values[i]);
        if (name == QStringLiteral("default")) m_enabled->setChecked(false);
        else m_enabled->setChecked(true);
        m_timer.stop();
        apply();
    }

    void scheduleApply() { m_timer.start(); }

    void apply()
    {
        if (!saveSharedConfig()) {
            m_status->setText(QStringLiteral("Não consegui salvar os ajustes"));
            return;
        }

        if (m_backend == QStringLiteral("xfce")) {
            if (waylandSession()) {
                m_status->setText(QStringLiteral("XFCE/Wayland · backend de cor indisponível neste compositor"));
                return;
            }
            applyXrandr();
            return;
        }

        if (m_backend == QStringLiteral("gnome") || m_backend == QStringLiteral("cinnamon")) {
            m_status->setText(platformBackendInstalled()
                                  ? (m_enabled->isChecked() ? QStringLiteral("Ajustes aplicados ao desktop") : QStringLiteral("Ajustes desativados"))
                                  : QStringLiteral("Instale o suporte do ambiente para ativar os ajustes"));
            return;
        }

        QSettings kwinConfig(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                                 + QStringLiteral("/kwinrc"), QSettings::IniFormat);
        const QString effectGroup = m_backend == QStringLiteral("kde")
            ? QStringLiteral("Effect-ajustevideo-app") : QStringLiteral("Effect-ajustevideo");
        kwinConfig.beginGroup(effectGroup);
        kwinConfig.setValue(QStringLiteral("Enabled"), m_enabled->isChecked());
        for (const Control &control : m_controls) {
            double value = control.key == QStringLiteral("Hue") ? control.value : control.value / 100.0;
            kwinConfig.setValue(control.key, value);
        }
        kwinConfig.endGroup();
        kwinConfig.sync();

        QDBusMessage message = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
            QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("reconfigureEffect"));
        message << QStringLiteral("ajustevideo-app");
        const auto reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 500);
        m_status->setText(reply.type() == QDBusMessage::ErrorMessage
                              ? QStringLiteral("KWin não respondeu · confira se o efeito foi instalado")
                              : (m_enabled->isChecked() ? QStringLiteral("Ajustes aplicados ao desktop") : QStringLiteral("Ajustes desativados")));
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
        const double brightness = 1.0 + (m_enabled->isChecked() ? m_controls[0].value / 100.0 : 0.0);
        const double gamma = m_enabled->isChecked() ? m_controls[2].value / 100.0 : 1.0;
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
    QString m_backend;
    QTimer m_timer;
    std::vector<Control> m_controls;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ajuste-video"));
    QApplication::setOrganizationName(QStringLiteral("xoykor"));
    VideoAdjustWindow window;
    window.show();
    return app.exec();
}
