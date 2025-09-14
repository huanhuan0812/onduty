#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QSettings>
#include <QFont>
#include <QGuiApplication>
#include <QCheckBox>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QDate>
#include <QWindow>
#include <windows.h>
#include <QTimer>
#include <QScreen>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QEnterEvent>  // 添加此头文件用于鼠标进入事件

class DutyRosterApp : public QWidget
{
    Q_OBJECT

public:
    DutyRosterApp(QWidget *parent = nullptr) : QWidget(parent)
    {
        // 设置配置文件路径为程序同目录
        configFilePath = QCoreApplication::applicationDirPath() + "/duty_config.ini";
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setupUI();
        setupTrayIcon();
        loadConfig();
        checkAndUpdateDuty(); // 在启动时检查并更新值日
        updateDisplay();
        positionToTopRight();

        // 设置初始透明度
        opacityEffect = new QGraphicsOpacityEffect(this);
        opacityEffect->setOpacity(0.8);  // 初始透明度
        this->setGraphicsEffect(opacityEffect);

        // 监听窗口状态变化
        connect(qApp, &QGuiApplication::focusWindowChanged, this, &DutyRosterApp::onFocusWindowChanged);

        // 设置PPT检测定时器
        pptCheckTimer = new QTimer(this);
        connect(pptCheckTimer, &QTimer::timeout, this, &DutyRosterApp::checkForPowerPointShow);
        pptCheckTimer->start(1500); // 每1.5秒检测一次

        // 设置30分钟检查定时器
        dutyCheckTimer = new QTimer(this);
        connect(dutyCheckTimer, &QTimer::timeout, this, &DutyRosterApp::checkDutyPeriodically);
        dutyCheckTimer->start(30 * 60 * 1000); // 每30分钟检查一次

        // 启用鼠标跟踪
        setMouseTracking(true);
    }

    ~DutyRosterApp()
    {
        saveConfig();
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (trayIcon->isVisible()) {
            hide();
            event->ignore();
        }
    }

    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        checkFullscreenState();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // 监听窗口状态变化事件
        if (event->type() == QEvent::WindowStateChange) {
            QWindow *window = qobject_cast<QWindow*>(watched);
            if (window) {
                checkFullscreenState();
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    // 添加鼠标进入事件处理
    void enterEvent(QEnterEvent *event) override
    {
        QWidget::enterEvent(event);
        animateOpacity(0.1);  // 鼠标移入时变为几乎透明
    }

    // 添加鼠标离开事件处理
    void leaveEvent(QEvent *event) override
    {
        QWidget::leaveEvent(event);
        animateOpacity(0.8);  // 鼠标离开时恢复透明度
    }

private slots:
    void rotateDuty()
    {
        if (checkAndUpdateDuty()) {
            QMessageBox::information(this, "值日已更新",
                QString("今天是%1，已更新值日安排。").arg(QDate::currentDate().toString("yyyy-MM-dd dddd")));
        } else {
            QMessageBox::information(this, "提示", "今天的值日已经安排过了或今天是周末!");
        }
    }

    void toggleVisibility()
    {
        if (isVisible()) {
            hide();
        } else {
            show();
            positionToTopRight();
        }
    }

    void quitApplication()
    {
        saveConfig();
        qApp->quit();
    }

    void iconActivated(QSystemTrayIcon::ActivationReason reason)
    {
        if (reason == QSystemTrayIcon::DoubleClick) {
            toggleVisibility();
        }
    }

    void onFocusWindowChanged(QWindow *focusWindow)
    {
        if (focusWindow) {
            // 监听焦点窗口的状态变化
            focusWindow->installEventFilter(this);
            checkFullscreenState();
        }
    }

    void checkFullscreenState()
    {
        bool hasFullscreen = false;

        // 检查所有窗口
        QList<QWindow*> windows = QGuiApplication::allWindows();
        for (QWindow *window : windows) {
            if (window && window->isVisible() &&
                (window->visibility() == QWindow::FullScreen ||
                 window->windowState() == Qt::WindowFullScreen)) {
                hasFullscreen = true;
                break;
            }
        }

        // 处理窗口显示/隐藏
        handleFullscreenState(hasFullscreen);
    }

    void handleFullscreenState(bool hasFullscreen)
    {
        // 如果有全屏应用且当前窗口可见，则隐藏
        if (hasFullscreen && isVisible()) {
            hide();
            wasHiddenByFullscreen = true;
        }
        // 如果没有全屏应用且之前是因为全屏被隐藏的，则显示
        else if (!hasFullscreen && wasHiddenByFullscreen && !isVisible()) {
            show();
            positionToTopRight();
            wasHiddenByFullscreen = false;
        }
    }

    // 检测PowerPoint是否正在放映
    void checkForPowerPointShow()
    {
        bool isPPTShowing = isPowerPointShowing();

        if (isPPTShowing && isVisible()) {
            // PPT正在放映，隐藏窗口
            QGraphicsOpacityEffect *opacityEffect = new QGraphicsOpacityEffect(this);
            QPropertyAnimation *animation = new QPropertyAnimation(opacityEffect, "opacity");
            animation->setDuration(1500); // 动画持续1.5秒
            animation->setStartValue(0.8);
            animation->setEndValue(0.0);
            animation->setEasingCurve(QEasingCurve::InOutQuad); // 设置缓动曲线
            animation->start();
            hide();
            wasHiddenByPPT = true;
            qDebug() << "检测到PowerPoint放映，隐藏窗口";
        }
        else if (!isPPTShowing && wasHiddenByPPT && !isVisible()) {
            // PPT放映结束，显示窗口
            show();
            QGraphicsOpacityEffect *opacityEffect = new QGraphicsOpacityEffect(this);
            QPropertyAnimation *animation = new QPropertyAnimation(opacityEffect, "opacity");
            animation->setDuration(1500); // 动画持续1.5秒
            animation->setStartValue(0.0);
            animation->setEndValue(0.8);
            animation->setEasingCurve(QEasingCurve::InOutQuad); // 设置缓动曲线
            animation->start();
            positionToTopRight();
            wasHiddenByPPT = false;
            qDebug() << "PowerPoint放映结束，显示窗口";
        }
    }

    // 定期检查值日更新
    void checkDutyPeriodically()
    {
        if (checkAndUpdateDuty()) {
            qDebug() << "定期检查：值日已更新 -" << QDateTime::currentDateTime().toString();
            updateDisplay();

            // 可选：显示通知消息
            if (trayIcon && trayIcon->isVisible()) {
                trayIcon->showMessage("值日已更新",
                    QString("已自动更新值日安排\n%1").arg(QDate::currentDate().toString("yyyy-MM-dd dddd")),
                    QSystemTrayIcon::Information, 3000);
            }
        }
    }

private:
    // 透明度动画函数
    void animateOpacity(qreal targetOpacity)
    {
        QPropertyAnimation *animation = new QPropertyAnimation(opacityEffect, "opacity");
        animation->setDuration(300); // 300毫秒的过渡动画
        animation->setStartValue(opacityEffect->opacity());
        animation->setEndValue(targetOpacity);
        animation->setEasingCurve(QEasingCurve::InOutQuad);
        animation->start(QPropertyAnimation::DeleteWhenStopped);
    }

    // 检测PowerPoint是否正在放映
    bool isPowerPointShowing()
    {
        // 方法1: 查找PowerPoint幻灯片放映窗口
        HWND hwnd = FindWindowW(L"screenClass", L"PowerPoint 幻灯片放映");
        if (hwnd && IsWindowVisible(hwnd)) {
            return true;
        }

        // 方法2: 通过窗口类名查找
        hwnd = FindWindowW(L"screenClass", nullptr);
        while (hwnd) {
            if (IsWindowVisible(hwnd)) {
                wchar_t windowTitle[256];
                GetWindowTextW(hwnd, windowTitle, 256);
                if (wcsstr(windowTitle, L"PowerPoint") != nullptr ||
                    wcsstr(windowTitle, L"幻灯片放映") != nullptr) {
                    return true;
                }
            }
            hwnd = FindWindowExW(nullptr, hwnd, L"screenClass", nullptr);
        }

        return false;
    }

    bool checkAndUpdateDuty()
    {
        QDate today = QDate::currentDate();
        QString todayStr = today.toString("yyyyMMdd");

        // 检查是否是工作日（周一到周五）
        if (today.dayOfWeek() >= 1 && today.dayOfWeek() <= 5)
        {
            // 检查配置文件中是否已经更新过今天的值日
            if (lastUpdateDate != todayStr)
            {
                // 轮换值日人员
                currentDutyIndex1 = (currentDutyIndex1 + 2) % 47;
                currentDutyIndex2 = (currentDutyIndex2 + 2) % 47;

                // 确保两个人不同
                while (currentDutyIndex1 == currentDutyIndex2)
                {
                    currentDutyIndex2 = (currentDutyIndex2 + 1) % 47;
                }

                // 更新日期
                lastUpdateDate = todayStr;

                // 保存配置
                saveConfig();
                updateDisplay();
                return true;
            }
        }
        return false;
    }

    void setupUI()
    {
        // 设置窗口属性
        setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        //setAttribute(Qt::WA_TransparentForMouseEvents, true);

        QWidget *mainWidget = new QWidget(this);
        mainWidget->setStyleSheet("QWidget { background-color: rgba(255, 255, 255, 200); border-radius: 10px; border: 2px solid #dddddd; }");

        QVBoxLayout *mainLayout = new QVBoxLayout(mainWidget);
        QVBoxLayout *outerLayout = new QVBoxLayout(this);
        outerLayout->addWidget(mainWidget);
        outerLayout->setContentsMargins(5, 5, 5, 5);

        // 标题栏
        QHBoxLayout *titleLayout = new QHBoxLayout();
        QLabel *titleLabel = new QLabel("值日安排");
        titleLabel->setStyleSheet("min-width: 60px;border: none;font-size: 14pt;background: none;");
        QFont titleFont = titleLabel->font();
        titleFont.setPointSize(14);
        titleFont.setBold(true);
        titleLabel->setFont(titleFont);

        titleLayout->addWidget(titleLabel);
        titleLayout->addStretch();
        mainLayout->addLayout(titleLayout);

        // 值日人员显示
        QHBoxLayout *dutyLayout = new QHBoxLayout();
        duty1Label = new QLabel();
        duty2Label = new QLabel();
        QFont dutyFont = duty1Label->font();
        dutyFont.setPointSize(18);
        dutyFont.setBold(true);
        duty1Label->setFont(dutyFont);
        duty2Label->setFont(dutyFont);
        duty1Label->setAlignment(Qt::AlignCenter);
        duty2Label->setAlignment(Qt::AlignCenter);

        QLabel *andlabel = new QLabel("&");
        andlabel->setStyleSheet("max-width: 15px;border: none;font-size: 12pt;font-weight: bold;background: none;");

        dutyLayout->addWidget(duty1Label);
        dutyLayout->addWidget(andlabel);
        dutyLayout->addWidget(duty2Label);
        mainLayout->addLayout(dutyLayout);

        setFixedSize(240, 140);
    }

    void setupTrayIcon()
    {
        trayIcon = new QSystemTrayIcon(this);
        trayIcon->setIcon(QIcon::fromTheme("preferences-system"));

        QMenu *trayMenu = new QMenu(this);
        QAction *toggleAction = new QAction("显示/隐藏", this);
        QAction *updateAction = new QAction("手动更新值日", this);
        QAction *quitAction = new QAction("退出", this);

        connect(toggleAction, &QAction::triggered, this, &DutyRosterApp::toggleVisibility);
        connect(updateAction, &QAction::triggered, this, &DutyRosterApp::rotateDuty);
        connect(quitAction, &QAction::triggered, this, &DutyRosterApp::quitApplication);
        connect(trayIcon, &QSystemTrayIcon::activated, this, &DutyRosterApp::iconActivated);

        trayMenu->addAction(toggleAction);
        trayMenu->addAction(updateAction);
        trayMenu->addAction(quitAction);
        trayIcon->setContextMenu(trayMenu);
        trayIcon->show();
    }

    void positionToTopRight()
    {
        QScreen *screen = QGuiApplication::primaryScreen();
        QRect screenGeometry = screen->geometry();
        int x = screenGeometry.width() - width() - 20;
        int y = 20;
        move(x, y);
    }

    void updateDisplay()
    {
        duty1Label->setText(QString::number(currentDutyIndex1 + 1));
        duty2Label->setText(QString::number(currentDutyIndex2 + 1));
    }

    void loadConfig()
    {
        QSettings config(configFilePath, QSettings::IniFormat);

        currentDutyIndex1 = config.value("duty/index1", 0).toInt();
        currentDutyIndex2 = config.value("duty/index2", 1).toInt();
        lastUpdateDate = config.value("date/lastUpdate", "").toString();

        // 如果配置文件不存在，创建默认配置
        if (!QFile::exists(configFilePath)) {
            saveConfig();
        }
    }

    void saveConfig()
    {
        QSettings config(configFilePath, QSettings::IniFormat);

        config.setValue("duty/index1", currentDutyIndex1);
        config.setValue("duty/index2", currentDutyIndex2);
        config.setValue("date/lastUpdate", lastUpdateDate);

        config.sync();
    }

    // 成员变量
    QLabel *duty1Label;
    QLabel *duty2Label;
    QSystemTrayIcon *trayIcon;
    QTimer *pptCheckTimer;
    QTimer *dutyCheckTimer;  // 新增：值日检查定时器
    QString configFilePath;
    bool wasHiddenByFullscreen = false;
    bool wasHiddenByPPT = false;
    QGraphicsOpacityEffect *opacityEffect;  // 添加透明度效果对象

    int currentDutyIndex1 = 0;
    int currentDutyIndex2 = 1;
    QString lastUpdateDate;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    DutyRosterApp window;
    window.show();
    
    return app.exec();
}

#include "main.moc"