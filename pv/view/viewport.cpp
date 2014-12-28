/*
 * This file is part of the PulseView project.
 *
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <cassert>
#include <cmath>
#include <algorithm>

#include "signal.hpp"
#include "view.hpp"
#include "viewitempaintparams.hpp"
#include "viewport.hpp"

#include <pv/session.hpp>

#include <QMouseEvent>

using std::abs;
using std::back_inserter;
using std::copy;
using std::max;
using std::min;
using std::none_of;
using std::shared_ptr;
using std::stable_sort;
using std::vector;

namespace pv {
namespace view {

Viewport::Viewport(View &parent) :
	ViewWidget(parent),
	mouse_down_valid_(false),
	pinch_zoom_active_(false)
{
	setAutoFillBackground(true);
	setBackgroundRole(QPalette::Base);
}

shared_ptr<ViewItem> Viewport::get_mouse_over_item(const QPoint &pt)
{
	const vector< shared_ptr<ViewItem> > items(this->items());
	for (auto i = items.rbegin(); i != items.rend(); i++)
		if ((*i)->enabled() &&
			(*i)->hit_box_rect(rect()).contains(pt))
			return *i;
	return nullptr;
}

vector< shared_ptr<ViewItem> > Viewport::items()
{
	vector< shared_ptr<ViewItem> > items(view_.begin(), view_.end());
	const vector< shared_ptr<TimeItem> > time_items(view_.time_items());
	copy(time_items.begin(), time_items.end(), back_inserter(items));
	return items;
}

bool Viewport::touch_event(QTouchEvent *event)
{
	QList<QTouchEvent::TouchPoint> touchPoints = event->touchPoints();

	if (touchPoints.count() != 2) {
		pinch_zoom_active_ = false;
		return false;
	}

	const QTouchEvent::TouchPoint &touchPoint0 = touchPoints.first();
	const QTouchEvent::TouchPoint &touchPoint1 = touchPoints.last();

	if (!pinch_zoom_active_ ||
	    (event->touchPointStates() & Qt::TouchPointPressed)) {
		pinch_offset0_ = view_.offset() + view_.scale() * touchPoint0.pos().x();
		pinch_offset1_ = view_.offset() + view_.scale() * touchPoint1.pos().x();
		pinch_zoom_active_ = true;
	}

	double w = touchPoint1.pos().x() - touchPoint0.pos().x();
	if (abs(w) >= 1.0) {
		double scale = (pinch_offset1_ - pinch_offset0_) / w;
		if (scale < 0)
			scale = -scale;
		double offset = pinch_offset0_ - touchPoint0.pos().x() * scale;
		if (scale > 0)
			view_.set_scale_offset(scale, offset);
	}

	if (event->touchPointStates() & Qt::TouchPointReleased) {
		pinch_zoom_active_ = false;

		if (touchPoint0.state() & Qt::TouchPointReleased) {
			// Primary touch released
			mouse_down_valid_ = false;
		} else {
			// Update the mouse down fields so that continued
			// dragging with the primary touch will work correctly
			mouse_down_point_ = touchPoint0.pos().toPoint();
			mouse_down_offset_ = view_.offset();
			mouse_down_valid_ = true;
		}
	}

	return true;
}

void Viewport::paintEvent(QPaintEvent*)
{
	vector< shared_ptr<RowItem> > row_items(view_.begin(), view_.end());
	assert(none_of(row_items.begin(), row_items.end(),
		[](const shared_ptr<RowItem> &r) { return !r; }));

	stable_sort(row_items.begin(), row_items.end(),
		[](const shared_ptr<RowItem> &a, const shared_ptr<RowItem> &b) {
			return a->visual_v_offset() < b->visual_v_offset(); });

	const vector< shared_ptr<TimeItem> > time_items(view_.time_items());
	assert(none_of(time_items.begin(), time_items.end(),
		[](const shared_ptr<TimeItem> &t) { return !t; }));

	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const ViewItemPaintParams pp(rect(), view_.scale(), view_.offset());

	for (const shared_ptr<TimeItem> t : time_items)
		t->paint_back(p, pp);
	for (const shared_ptr<RowItem> r : row_items)
		r->paint_back(p, pp);

	for (const shared_ptr<TimeItem> t : time_items)
		t->paint_mid(p, pp);
	for (const shared_ptr<RowItem> r : row_items)
		r->paint_mid(p, pp);

	for (const shared_ptr<RowItem> r : row_items)
		r->paint_fore(p, pp);
	for (const shared_ptr<TimeItem> t : time_items)
		t->paint_fore(p, pp);

	p.end();
}

void Viewport::mousePressEvent(QMouseEvent *event)
{
	assert(event);

	if (event->button() == Qt::LeftButton) {
		mouse_down_point_ = event->pos();
		mouse_down_offset_ = view_.offset();
		mouse_down_valid_ = true;
	}
}

void Viewport::mouseReleaseEvent(QMouseEvent *event)
{
	assert(event);

	if (event->button() == Qt::LeftButton)
		mouse_down_valid_ = false;
}

void Viewport::mouseMoveEvent(QMouseEvent *event)
{
	assert(event);

	if (event->buttons() & Qt::LeftButton) {
		if (!mouse_down_valid_) {
			mouse_down_point_ = event->pos();
			mouse_down_offset_ = view_.offset();
			mouse_down_valid_ = true;
		}

		view_.set_scale_offset(view_.scale(),
			mouse_down_offset_ +
			(mouse_down_point_ - event->pos()).x() *
			view_.scale());
	}
}

void Viewport::mouseDoubleClickEvent(QMouseEvent *event)
{
	assert(event);

	if (event->buttons() & Qt::LeftButton)
		view_.zoom(2.0, event->x());
	else if (event->buttons() & Qt::RightButton)
		view_.zoom(-2.0, event->x());
}

void Viewport::wheelEvent(QWheelEvent *event)
{
	assert(event);

	if (event->orientation() == Qt::Vertical) {
		// Vertical scrolling is interpreted as zooming in/out
		view_.zoom(event->delta() / 120, event->x());
	} else if (event->orientation() == Qt::Horizontal) {
		// Horizontal scrolling is interpreted as moving left/right
		view_.set_scale_offset(view_.scale(),
				       event->delta() * view_.scale()
				       + view_.offset());
	}
}

} // namespace view
} // namespace pv
