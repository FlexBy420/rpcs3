#pragma once

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QRadialGradient>
#include <QSvgRenderer>
#include <QWidget>
#include <QResizeEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

class controller_live_preview final : public QWidget
{
public:
	enum class button : int
	{
		dpad_left,
		dpad_down,
		dpad_right,
		dpad_up,
		l1,
		l2,
		l3,
		select,
		start,
		ps,
		r1,
		r2,
		r3,
		square,
		cross,
		circle,
		triangle,
		count
	};

	struct state
	{
		bool connected = false;
		std::array<int, static_cast<int>(button::count)> buttons{};
		int lx = 0;
		int ly = 0;
		int rx = 0;
		int ry = 0;
		int stick_max = 255;
	};

	explicit controller_live_preview(QWidget* parent, QColor base_color = {})
		: QWidget(parent)
		, m_base_color(base_color.isValid() ? base_color : palette().color(QPalette::WindowText))
	{
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_TranslucentBackground);
		setFocusPolicy(Qt::NoFocus);

		if (parent)
		{
			parent->installEventFilter(this);
			sync_geometry();
		}
	}

	void set_state(const state& new_state)
	{
		m_state = new_state;
		m_state.stick_max = std::max(1, m_state.stick_max);
		sync_geometry();
		raise();
		update();
	}

	// Keep the controller visible when the device disconnects, only clear live input.
	void clear_input()
	{
		m_state = {};
		m_state.stick_max = 255;
		update();
	}

	void set_base_color(const QColor& color)
	{
		if (!color.isValid() || color == m_base_color)
		{
			return;
		}

		m_base_color = color;
		m_static_layer = QPixmap();
		update();
	}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched == parentWidget())
		{
			switch (event->type())
			{
			case QEvent::Resize:
			case QEvent::Show:
			case QEvent::LayoutRequest:
				sync_geometry();
				break;
			case QEvent::PaletteChange:
				m_static_layer = QPixmap();
				break;
			default:
				break;
			}
		}

		return QWidget::eventFilter(watched, event);
	}

	void resizeEvent(QResizeEvent* event) override
	{
		m_static_layer = QPixmap();
		QWidget::resizeEvent(event);
	}

	void paintEvent(QPaintEvent* event) override
	{
		Q_UNUSED(event);

		if (width() <= 0 || height() <= 0)
		{
			return;
		}

		ensure_static_layer();

		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
		painter.drawPixmap(rect(), m_static_layer, m_static_layer.rect());

		// All dynamic geometry below uses the original DualShock_3.svg viewbox.
		painter.save();
		painter.scale(width() / 400.0, height() / 250.0);

		const QColor accent = palette().color(QPalette::Highlight);
		const QColor base = m_base_color;

		const auto strength = [this](button id) -> qreal
		{
			if (!m_state.connected)
			{
				return 0.0;
			}

			return std::clamp(m_state.buttons[static_cast<int>(id)] / 255.0, 0.0, 1.0);
		};

		const auto mixed_color = [](const QColor& a, const QColor& b, qreal amount)
		{
			amount = std::clamp(amount, 0.0, 1.0);
			return QColor::fromRgbF(
				a.redF()   * (1.0 - amount) + b.redF()   * amount,
				a.greenF() * (1.0 - amount) + b.greenF() * amount,
				a.blueF()  * (1.0 - amount) + b.blueF()  * amount,
				a.alphaF() * (1.0 - amount) + b.alphaF() * amount);
		};

		const auto pressed_fill = [&accent](qreal amount)
		{
			QColor color = accent;
			color.setAlphaF(0.10 + 0.78 * std::pow(std::clamp(amount, 0.0, 1.0), 0.72));
			return color;
		};

		const auto configure_button_painter = [&](qreal amount)
		{
			const QColor outline = mixed_color(base, accent, amount * 0.72);
			painter.setPen(QPen(outline, 1.0 + amount * 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			if (amount > 0.0)
			{
				painter.setBrush(QBrush(pressed_fill(amount)));
			}
			else
			{
				painter.setBrush(Qt::NoBrush);
			}
		};

		const auto with_press_transform = [&](const QPointF& center, qreal amount, const auto& draw)
		{
			painter.save();
			const qreal scale = 1.0 - 0.055 * amount;
			painter.translate(center.x(), center.y() + 2.3 * amount);
			painter.scale(scale, scale);
			painter.translate(-center.x(), -center.y());
			draw();
			painter.restore();
		};

		const auto draw_face_button = [&](button id, const QPointF& center, const auto& draw_symbol)
		{
			const qreal amount = strength(id);
			with_press_transform(center, amount, [&]
			{
				configure_button_painter(amount);
				painter.drawEllipse(center, 13.0, 13.0);
				painter.setBrush(Qt::NoBrush);
				painter.setPen(QPen(mixed_color(base, accent, amount * 0.92), 1.35 + amount * 0.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
				draw_symbol();
			});
		};

		draw_face_button(button::square, QPointF(289.5, 93.5), [&]
		{
			painter.drawRect(QRectF(282.0, 86.5, 15.0, 15.0));
		});

		draw_face_button(button::circle, QPointF(347.5, 93.5), [&]
		{
			painter.drawEllipse(QPointF(347.5, 93.5), 9.0, 9.0);
		});

		draw_face_button(button::triangle, QPointF(318.5, 64.5), [&]
		{
			QPainterPath triangle;
			triangle.moveTo(310.0, 70.0);
			triangle.lineTo(327.0, 70.0);
			triangle.lineTo(318.5, 56.0);
			triangle.closeSubpath();
			painter.drawPath(triangle);
		});

		draw_face_button(button::cross, QPointF(318.5, 122.5), [&]
		{
			painter.drawLine(QPointF(311.5, 115.5), QPointF(325.5, 129.5));
			painter.drawLine(QPointF(311.5, 129.5), QPointF(325.5, 115.5));
		});

		const auto draw_dpad = [&](button id, const QPointF& center, const QPainterPath& path)
		{
			const qreal amount = strength(id);
			with_press_transform(center, amount, [&]
			{
				configure_button_painter(amount);
				painter.drawPath(path);
			});
		};

		painter.save();
		painter.setPen(QPen(base, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.setBrush(Qt::NoBrush);

		QPainterPath dpad_decorations;
		dpad_decorations.moveTo(48.0, 88.0);
		dpad_decorations.lineTo(48.0, 99.0);
		dpad_decorations.lineTo(43.0, 93.5);
		dpad_decorations.closeSubpath();
		dpad_decorations.moveTo(119.0, 88.0);
		dpad_decorations.lineTo(119.0, 99.0);
		dpad_decorations.lineTo(124.0, 93.5);
		dpad_decorations.closeSubpath();
		dpad_decorations.moveTo(78.0, 58.0);
		dpad_decorations.lineTo(89.0, 58.0);
		dpad_decorations.lineTo(83.5, 53.0);
		dpad_decorations.closeSubpath();
		dpad_decorations.moveTo(78.0, 129.0);
		dpad_decorations.lineTo(89.0, 129.0);
		dpad_decorations.lineTo(83.5, 134.0);
		dpad_decorations.closeSubpath();
		painter.drawPath(dpad_decorations);
		painter.restore();

		QPainterPath dpad_left;
		dpad_left.moveTo(52.0, 85.0);
		dpad_left.lineTo(69.0, 85.0);
		dpad_left.lineTo(75.0, 93.5);
		dpad_left.lineTo(69.0, 102.0);
		dpad_left.lineTo(52.0, 102.0);
		dpad_left.closeSubpath();

		QPainterPath dpad_right;
		dpad_right.moveTo(115.0, 85.0);
		dpad_right.lineTo(98.0, 85.0);
		dpad_right.lineTo(92.0, 93.5);
		dpad_right.lineTo(98.0, 102.0);
		dpad_right.lineTo(115.0, 102.0);
		dpad_right.closeSubpath();

		QPainterPath dpad_up;
		dpad_up.moveTo(75.0, 62.0);
		dpad_up.lineTo(75.0, 79.0);
		dpad_up.lineTo(83.5, 85.0);
		dpad_up.lineTo(92.0, 79.0);
		dpad_up.lineTo(92.0, 62.0);
		dpad_up.closeSubpath();

		QPainterPath dpad_down;
		dpad_down.moveTo(75.0, 125.0);
		dpad_down.lineTo(75.0, 108.0);
		dpad_down.lineTo(83.5, 102.0);
		dpad_down.lineTo(92.0, 108.0);
		dpad_down.lineTo(92.0, 125.0);
		dpad_down.closeSubpath();

		draw_dpad(button::dpad_left,  QPointF(59.0, 93.5),  dpad_left);
		draw_dpad(button::dpad_right, QPointF(108.0, 93.5), dpad_right);
		draw_dpad(button::dpad_up,    QPointF(83.5, 69.0),  dpad_up);
		draw_dpad(button::dpad_down,  QPointF(83.5, 118.0), dpad_down);

		const auto draw_center_button = [&](button id, const QPointF& center, const auto& draw)
		{
			const qreal amount = strength(id);
			with_press_transform(center, amount, [&]
			{
				configure_button_painter(amount);
				draw();
			});
		};

		draw_center_button(button::select, QPointF(165.0, 100.0), [&]
		{
			painter.drawRect(QRectF(155.0, 95.0, 20.0, 10.0));
		});

		draw_center_button(button::start, QPointF(235.0, 100.0), [&]
		{
			QPainterPath start;
			start.moveTo(226.0, 95.0);
			start.lineTo(226.0, 105.0);
			start.lineTo(244.0, 100.0);
			start.closeSubpath();
			painter.drawPath(start);
		});

		draw_center_button(button::ps, QPointF(201.0, 129.0), [&]
		{
			painter.drawEllipse(QPointF(201.0, 129.0), 13.0, 13.0);
		});

		const auto draw_shoulder_contour = [&](button id, bool left, bool rear)
		{
			const qreal amount = strength(id);
			QPainterPath shoulder;

			if (left)
			{
				if (rear)
				{
					shoulder.moveTo(58.0, 12.0);
					shoulder.cubicTo(69.0, 3.5, 99.0, 3.5, 110.0, 12.0);
				}
				else
				{
					shoulder.moveTo(60.5, 10.6);
					shoulder.cubicTo(74.8, 6.5, 94.4, 6.7, 107.0, 10.6);
				}
			}
			else
			{
				if (rear)
				{
					shoulder.moveTo(293.0, 12.0);
					shoulder.cubicTo(304.0, 3.5, 334.0, 3.5, 345.0, 12.0);
				}
				else
				{
					shoulder.moveTo(295.5, 10.6);
					shoulder.cubicTo(309.8, 6.5, 329.4, 6.7, 343.7, 11.2);
				}
			}

			const QColor shoulder_color = mixed_color(base, accent, amount > 0.0 ? 0.45 + amount * 0.55 : 0.0);
			painter.save();
			painter.translate(0.0, rear ? amount * 3.3 : amount * 1.8);
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(shoulder_color, 1.0 + amount * (rear ? 4.2 : 2.8), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPath(shoulder);
			painter.restore();
		};

		draw_shoulder_contour(button::l2, true,  true);
		draw_shoulder_contour(button::r2, false, true);
		draw_shoulder_contour(button::l1, true,  false);
		draw_shoulder_contour(button::r1, false, false);

		const auto draw_shoulder_face = [&](button id, const QRectF& resting_rect, const QString& label, bool trigger)
		{
			const qreal amount = strength(id);
			const qreal travel = (trigger ? 5.0 : 2.5) * amount;
			QRectF face = resting_rect.translated(0.0, travel);
			face.setHeight(std::max<qreal>(5.0, face.height() - (trigger ? 3.2 : 1.5) * amount));

			QColor fill = base;
			fill.setAlphaF(0.035 + 0.14 * amount);
			if (amount > 0.0)
			{
				fill = pressed_fill(amount);
			}

			painter.save();
			painter.setBrush(fill);
			painter.setPen(QPen(mixed_color(base, accent, amount * 0.9), 1.0 + amount * 1.3));
			painter.drawRoundedRect(face, 3.0, 3.0);

			if (amount > 0.0)
			{
				QRectF meter(face.left() + 2.0, face.bottom() - 3.0, (face.width() - 4.0) * amount, 2.0);
				QColor meter_color = accent;
				meter_color.setAlphaF(0.85);
				painter.setPen(Qt::NoPen);
				painter.setBrush(meter_color);
				painter.drawRoundedRect(meter, 1.0, 1.0);
			}

			QFont font = painter.font();
			font.setBold(true);
			font.setPixelSize(9);
			painter.setFont(font);
			painter.setPen(mixed_color(base, accent, amount));
			painter.drawText(face, Qt::AlignCenter, label);
			painter.restore();
		};

		// Front face: rear triggers on the outside, bumpers toward the center.
		draw_shoulder_face(button::l2, QRectF(122.0, 5.0, 34.0, 16.0), QStringLiteral("L2"), true);
		draw_shoulder_face(button::l1, QRectF(159.0, 8.0, 34.0, 13.0), QStringLiteral("L1"), false);
		draw_shoulder_face(button::r1, QRectF(207.0, 8.0, 34.0, 13.0), QStringLiteral("R1"), false);
		draw_shoulder_face(button::r2, QRectF(244.0, 5.0, 34.0, 16.0), QStringLiteral("R2"), true);

		const auto draw_stick = [&](const QPointF& base_center, int x, int y, button click_id)
		{
			const qreal max_value = static_cast<qreal>(std::max(1, m_state.stick_max));
			const qreal norm_x = m_state.connected ? std::clamp(x / max_value, -1.0, 1.0) : 0.0;
			const qreal norm_y = m_state.connected ? std::clamp(y / max_value, -1.0, 1.0) : 0.0;

			constexpr qreal travel = 11.5;
			const QPointF center(base_center.x() + norm_x * travel, base_center.y() - norm_y * travel);
			const qreal click = strength(click_id);
			const qreal cap_scale = 1.0 - click * 0.065;

			painter.save();
			painter.translate(center);
			painter.scale(cap_scale, cap_scale);
			painter.translate(-center);

			if (click > 0.0)
			{
				QRadialGradient gradient(center - QPointF(5.0, 6.0), 28.0);
				QColor inner = accent;
				inner.setAlphaF(0.20 + click * 0.62);
				QColor outer = accent;
				outer.setAlphaF(0.04 + click * 0.24);
				gradient.setColorAt(0.0, inner);
				gradient.setColorAt(1.0, outer);
				painter.setBrush(gradient);
			}
			else
			{
				painter.setBrush(Qt::NoBrush);
			}

			painter.setPen(QPen(mixed_color(base, accent, click * 0.72), 1.0 + click * 1.4));
			painter.drawEllipse(center, 30.0, 30.0);
			painter.drawEllipse(center, 27.0, 27.0);
			painter.restore();
		};

		draw_stick(QPointF(143.4, 154.0), m_state.lx, m_state.ly, button::l3);
		draw_stick(QPointF(258.0, 154.0), m_state.rx, m_state.ry, button::r3);

		painter.restore();
	}

private:
	void sync_geometry()
	{
		if (QWidget* parent = parentWidget())
		{
			setGeometry(parent->rect());
		}
	}

	void ensure_static_layer()
	{
		const qreal dpr = devicePixelRatioF();
		const QSize pixel_size(std::max(1, qRound(width() * dpr)), std::max(1, qRound(height() * dpr)));

		if (!m_static_layer.isNull() && m_static_layer.size() == pixel_size && qFuzzyCompare(m_static_layer.devicePixelRatio(), dpr))
		{
			return;
		}

		QSvgRenderer renderer(QStringLiteral(":/Icons/DualShock_3.svg"));
		QPixmap layer(pixel_size);
		layer.fill(Qt::transparent);

		QPainter painter(&layer);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
		painter.scale(pixel_size.width() / 400.0, pixel_size.height() / 250.0);

		// Everything except elements that must move/depress at runtime.
		static constexpr std::array<const char*, 12> static_elements =
		{
			"MainOutline",
			"LeftShoulder",
			"RightShoulder",
			"UpperLine1",
			"UpperLine2",
			"LowerLine1",
			"LowerLine2",
			"SelectText",
			"StartText",
			"RightJoystickOuter",
			"LeftJoystickOuter",
			"RightFacebuttonCircle",
		};

		for (const char* id : static_elements)
		{
			const QString element_id = QString::fromLatin1(id);
			if (renderer.elementExists(element_id))
			{
				renderer.render(&painter, element_id, renderer.boundsOnElement(element_id));
			}
		}

		// These two borders are static housings around the dynamic button surfaces.
		for (const QString& element_id : {QStringLiteral("RightButtonBorder"), QStringLiteral("LeftDpadCircle"), QStringLiteral("LeftDpadBorder")})
		{
			if (renderer.elementExists(element_id))
			{
				renderer.render(&painter, element_id, renderer.boundsOnElement(element_id));
			}
		}

		painter.end();

		QPainter tint(&layer);
		tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
		tint.fillRect(layer.rect(), m_base_color);
		tint.end();

		layer.setDevicePixelRatio(dpr);
		m_static_layer = std::move(layer);
	}

	state m_state;
	QColor m_base_color;
	QPixmap m_static_layer;
};
