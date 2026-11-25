#include "ControllerDiagram.h"

#include <QApplication>
#include <QEnterEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
    // The picture sits in the middle of the layout space, scaled up from its 460x302 pixels.
    constexpr double kPictureScale = 1.25;
    constexpr double kPictureTop = 112;
}

// ---- BindingBox -----------------------------------------------------------------------------

BindingBox::BindingBox(const QString &title, QWidget *parent) : QFrame(parent)
{
    m_title = new QLabel(title);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    m_keys = new QLabel;
    m_controller = new QLabel;
    for (QLabel *label : {m_title, m_keys, m_controller})
    {
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setTextFormat(Qt::RichText);
    }
    m_title->setTextFormat(Qt::PlainText);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(0);
    layout->addWidget(m_title);
    layout->addWidget(m_keys);
    layout->addWidget(m_controller);

    setCursor(Qt::PointingHandCursor);
    setToolTip("Click, then press a key or a controller button, trigger or stick direction.\n"
               "Right-click to clear.");
    updateStyle();
}

void BindingBox::setBindings(const QString &keys, const QString &controller)
{
    const auto line = [](const char *kind, const QString &value) {
        return QString("<span style='color:gray'>%1</span>&nbsp;%2").arg(kind, value.toHtmlEscaped());
    };
    m_keys->setText(line("Key", keys));
    m_controller->setText(line("Pad", controller));
}

void BindingBox::setWaiting(bool waiting)
{
    m_waiting = waiting;
    if (waiting)
    {
        m_keys->setText("<i>Press a key</i>");
        m_controller->setText("<i>or a controller input...</i>");
    }
    updateStyle();
}

void BindingBox::setAccent(const QColor &color)
{
    m_accent = color;
    updateStyle();
}

void BindingBox::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked();
    else if (event->button() == Qt::RightButton)
        emit contextMenuRequested(event->globalPosition().toPoint());
}

void BindingBox::enterEvent(QEnterEvent *event)
{
    QFrame::enterEvent(event);
    m_hover = true;
    updateStyle();
}

void BindingBox::leaveEvent(QEvent *event)
{
    QFrame::leaveEvent(event);
    m_hover = false;
    updateStyle();
}

void BindingBox::updateStyle()
{
    // The application's palette: a box's own palette takes on its style sheet's colors.
    const QPalette colors = QApplication::palette();
    const QColor accent = m_accent.isValid() ? m_accent : colors.color(QPalette::Highlight);
    const QColor base = colors.color(QPalette::Button);
    const QColor border = m_waiting ? accent : m_hover ? accent.lighter(130) : colors.color(QPalette::Mid);
    const QColor fill = m_waiting ? accent.darker(300) : base;
    const QString stripe = m_accent.isValid() ? QString(" border-left: 4px solid %1;").arg(m_accent.name()) : QString();
    setStyleSheet(QString("BindingBox { background: %1; border: %2px solid %3; border-radius: 6px;%4 }")
                      .arg(fill.name(), m_waiting ? "3" : "1", border.name(), stripe));
}

// ---- ControllerDiagram ----------------------------------------------------------------------

ControllerDiagram::ControllerDiagram(QWidget *parent) : QWidget(parent), m_picture(":/controller.png")
{
    setMinimumSize(static_cast<int>(kWidth), static_cast<int>(kHeight));
}

void ControllerDiagram::addBox(BindingBox *box, const QRectF &rect, const QPointF &anchor, const QColor &color)
{
    box->setAccent(color);
    box->setParent(this);
    m_boxes.push_back({box, rect, anchor, color});
    box->show();
}

void ControllerDiagram::addGroup(const QString &title, const QRectF &rect, const QPointF &anchor, const QColor &color)
{
    m_groups.push_back({title, rect, anchor, color});
}

double ControllerDiagram::scale() const
{
    return std::min(width() / kWidth, height() / kHeight);
}

QPointF ControllerDiagram::origin() const
{
    const double s = scale();
    return {(width() - kWidth * s) / 2, (height() - kHeight * s) / 2};
}

QRectF ControllerDiagram::imageRect() const
{
    const double w = m_picture.width() * kPictureScale;
    const double h = m_picture.height() * kPictureScale;
    return {(kWidth - w) / 2, kPictureTop, w, h};
}

void ControllerDiagram::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const double s = scale();
    const QPointF o = origin();
    for (const Placed &placed : m_boxes)
    {
        const QRectF r(o + placed.rect.topLeft() * s, placed.rect.size() * s);
        placed.box->setGeometry(r.toAlignedRect());
    }
}

void ControllerDiagram::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const double s = scale();
    painter.translate(origin());
    painter.scale(s, s);

    // A soft light backdrop keeps the black controller visible in a dark theme.
    const QRectF image = imageRect();
    QColor backdrop = palette().color(QPalette::Window).lighter(135);
    painter.setPen(Qt::NoPen);
    painter.setBrush(backdrop);
    painter.drawEllipse(image.adjusted(-30, -20, 30, 20));
    painter.drawPixmap(image, m_picture, QRectF(m_picture.rect()));

    const QColor accent = palette().color(QPalette::Highlight);
    const auto connect = [&](const QRectF &r, const QPointF &anchor, const QColor &color) {
        const QColor line = color.isValid() ? color : accent;
        const QPointF target = image.topLeft() + anchor * kPictureScale;
        const QPointF from(std::clamp(target.x(), r.left(), r.right()), std::clamp(target.y(), r.top(), r.bottom()));
        painter.setPen(QPen(line, 2));
        painter.drawLine(from, target);
        painter.setPen(Qt::NoPen);
        painter.setBrush(line);
        painter.drawEllipse(target, 4, 4);
    };
    for (const Group &group : m_groups)
    {
        painter.setPen(QPen(palette().color(QPalette::Mid), 1));
        painter.setBrush(palette().color(QPalette::Window));
        painter.drawRoundedRect(group.rect, 8, 8);
        QFont bold = font();
        bold.setBold(true);
        painter.setFont(bold);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(group.rect.adjusted(10, 2, -10, 0), Qt::AlignLeft | Qt::AlignTop, group.title);
        connect(group.rect, group.anchor, group.color);
    }
    for (const Placed &placed : m_boxes)
    {
        if (placed.anchor.x() >= 0)
            connect(placed.rect, placed.anchor, placed.color);
    }
}
