#include "hud.h"

#include "view_cube.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QRegion>
#include <QTimer>
#include <QToolButton>

Hud::Hud(QWidget* window, QWidget* target) : QWidget(window), window_(window), target_(target) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);

    bar_ = new QFrame(this);
    bar_->setObjectName(QStringLiteral("hudBar"));
    barLayout_ = new QHBoxLayout(bar_);
    barLayout_->setContentsMargins(8, 6, 8, 6);
    barLayout_->setSpacing(2);

    pill_ = new QFrame(this);
    pill_->setObjectName(QStringLiteral("hudPill"));
    pillLayout_ = new QHBoxLayout(pill_);
    pillLayout_->setContentsMargins(14, 6, 14, 6);
    pillLayout_->setSpacing(10);
    pillTitle_ = new QLabel(pill_);
    pillTitle_->setObjectName(QStringLiteral("hudPillTitle"));
    pillHint_ = new QLabel(tr("Esc to exit"), pill_);
    pillHint_->setObjectName(QStringLiteral("hudHint"));
    pillLayout_->addWidget(pillTitle_);
    pillLayout_->addWidget(pillHint_);
    pill_->hide();

    toast_ = new QLabel(this);
    toast_->setObjectName(QStringLiteral("hudToast"));
    toast_->hide();

    cube_ = new ViewCube(this);

    window_->installEventFilter(this);
    target_->installEventFilter(this);
    setLight(true);
}

void Hud::setLight(bool light) {
    light_ = light;
    cube_->setLight(light);
    // Paper-white glass on a light view, Graphite glass on a dark one. The one accent is Signal orange.
    const QString glass = light ? QStringLiteral("rgba(255,255,255,235)") : QStringLiteral("rgba(30,33,39,238)");
    const QString edge = light ? QStringLiteral("rgba(17,19,22,38)") : QStringLiteral("rgba(243,241,234,40)");
    const QString text = light ? QStringLiteral("#111316") : QStringLiteral("#F3F1EA");
    const QString hover = light ? QStringLiteral("rgba(17,19,22,20)") : QStringLiteral("rgba(243,241,234,28)");
    const QString dim = light ? QStringLiteral("#6B6E73") : QStringLiteral("#9A9C9F");
    setStyleSheet(QStringLiteral(
        "#hudBar, #hudPill { background:%1; border:1px solid %2; border-radius:14px; }"
        "#hudPill { border-radius:18px; }"
        "#hudToast { background:%1; border:1px solid %2; border-radius:12px; padding:6px 14px; color:%3; }"
        "QLabel { color:%3; background:transparent; }"
        "#hudPillTitle { font-weight:600; }"
        "#hudHint { color:%5; font-size:11px; }"
        "QToolButton { color:%3; background:transparent; border:0; border-radius:9px; padding:5px 6px; font-size:11px; }"
        "#hudBar QToolButton { min-width:54px; }"
        "QToolButton:hover { background:%4; }"
        "QToolButton:checked { background:#FF5B1F; color:#111316; }"
        "QToolButton:disabled { color:%5; }"
        "QToolButton::menu-indicator { image:none; }"
        "QSlider::groove:horizontal { height:4px; background:%2; border-radius:2px; }"
        "QSlider::sub-page:horizontal { background:#FF5B1F; border-radius:2px; }"
        "QSlider::handle:horizontal { width:14px; margin:-5px 0; border-radius:7px; background:%3; }")
                      .arg(glass, edge, text, hover, dim));
}

QToolButton* Hud::addButton(const QIcon& icon, const QString& label, const QString& tip, bool checkable, bool gap) {
    if (gap) {
        auto* line = new QFrame(bar_);
        line->setFixedSize(1, 30);
        line->setStyleSheet(QStringLiteral("background:rgba(128,128,128,80);"));
        barLayout_->addSpacing(5);
        barLayout_->addWidget(line);
        barLayout_->addSpacing(5);
    }
    auto* b = new QToolButton(bar_);
    b->setIcon(icon);
    b->setIconSize(QSize(22, 22));
    b->setText(label);
    b->setToolTip(tip);
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setCheckable(checkable);
    b->setFocusPolicy(Qt::NoFocus);
    b->setCursor(Qt::ArrowCursor);
    barLayout_->addWidget(b);
    return b;
}

void Hud::setPill(const QString& title, QWidget* content) {
    if (pillContent_) {
        pillLayout_->removeWidget(pillContent_);
        pillContent_->hide();
        pillContent_->setParent(nullptr);
        pillContent_ = nullptr;
    }
    if (content) {
        pillTitle_->setText(title);
        content->setParent(pill_);
        pillLayout_->insertWidget(1, content);
        content->show();
        pillContent_ = content;
    }
    pill_->setVisible(content != nullptr);
    relayout();
}

void Hud::toast(const QString& text) {
    toast_->setText(text);
    toast_->adjustSize();
    toast_->show();
    relayout();
    QTimer::singleShot(2600, toast_, [this, text] {
        if (toast_->text() != text) return;  // a newer message is showing
        toast_->hide();
        relayout();
    });
}

void Hud::relayout() {
    const bool wanted = target_->isVisible() && window_->isVisible() && !window_->isMinimized();
    if (!wanted) { hide(); return; }
    const QRect area(target_->mapToGlobal(QPoint(0, 0)), target_->size());
    if (geometry() != area) setGeometry(area);

    bar_->adjustSize();
    bar_->move((width() - bar_->width()) / 2, height() - bar_->height() - 18);
    cube_->move(width() - cube_->width() - 14, 12);
    QRegion mask = QRegion(bar_->geometry()) | QRegion(cube_->geometry());
    if (pill_->isVisible()) {
        pill_->adjustSize();
        pill_->move((width() - pill_->width()) / 2, 14);
        mask |= pill_->geometry();
    }
    if (toast_->isVisible()) {
        toast_->move((width() - toast_->width()) / 2, bar_->y() - toast_->height() - 10);
        mask |= toast_->geometry();
    }
    setMask(mask);
    if (!isVisible()) show();
}

bool Hud::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::WindowStateChange:
        // After the window has settled into its new place.
        QTimer::singleShot(0, this, &Hud::relayout);
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}
