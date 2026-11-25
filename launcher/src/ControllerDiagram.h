#pragma once

#include <QColor>
#include <QFrame>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <vector>

class QLabel;

// One PS2 input on the diagram: its name, then the key and controller input bound to it.
// Clicking it starts listening for either; right-clicking offers to clear them.
class BindingBox : public QFrame
{
    Q_OBJECT

public:
    explicit BindingBox(const QString &title, QWidget *parent = nullptr);

    void setBindings(const QString &keys, const QString &controller);
    void setWaiting(bool waiting);
    // The color of the box's line, shown as a stripe down its left edge.
    void setAccent(const QColor &color);

signals:
    void clicked();
    void contextMenuRequested(const QPoint &globalPos);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void updateStyle();

    QLabel *m_title = nullptr;
    QLabel *m_keys = nullptr;
    QLabel *m_controller = nullptr;
    bool m_waiting = false;
    bool m_hover = false;
    QColor m_accent;
};

// The controller picture with boxes placed around it, each joined by a line to the part it
// binds. Positions are in a fixed layout space that scales with the widget.
class ControllerDiagram : public QWidget
{
    Q_OBJECT

public:
    static constexpr double kWidth = 980;
    static constexpr double kHeight = 620;

    explicit ControllerDiagram(QWidget *parent = nullptr);

    // rect is in layout space; anchor is a pixel in the controller picture. A box without an
    // anchor gets no line (it sits in a group that has one).
    void addBox(BindingBox *box, const QRectF &rect, const QPointF &anchor = {-1, -1}, const QColor &color = {});
    // A titled frame around several boxes, with one line to the picture.
    void addGroup(const QString &title, const QRectF &rect, const QPointF &anchor, const QColor &color = {});

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Placed
    {
        BindingBox *box;
        QRectF rect;
        QPointF anchor;
        QColor color;
    };

    double scale() const;
    QPointF origin() const;
    QRectF imageRect() const; // in layout space

    struct Group
    {
        QString title;
        QRectF rect;
        QPointF anchor;
        QColor color;
    };

    QPixmap m_picture;
    std::vector<Placed> m_boxes;
    std::vector<Group> m_groups;
};
