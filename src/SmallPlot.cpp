/*
 * Copyright (c) 2011 Damien Grauser
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "SmallPlot.h"
#include "RideFile.h"
#include "RideItem.h"
#include "Settings.h"
#include "Colors.h"

#include <assert.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_marker.h>
#include <qwt_text.h>
#include <qwt_legend.h>
#include <qwt_series_data.h>
#include <qwt_compat.h>

static double inline max(double a, double b) { if (a > b) return a; else return b; }

#define MILES_PER_KM 0.62137119

SmallPlot::SmallPlot(QWidget *parent) : QwtPlot(parent), d_mrk(NULL), smooth(30)
{
    setCanvasBackground(GColor(CPLOTBACKGROUND));
    canvas()->setFrameStyle(QFrame::NoFrame);

    setXTitle();

    wattsCurve = new QwtPlotCurve("Power");

    //timeCurves.resize(36);// wattsCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
    wattsCurve->setPen(QPen(GColor(CPOWER)));
    wattsCurve->attach(this);

    hrCurve = new QwtPlotCurve("Heart Rate");
    // hrCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
    hrCurve->setPen(QPen(GColor(CHEARTRATE)));
    hrCurve->attach(this);

    // grid lines on such a small plot look AWFUL
    //grid = new QwtPlotGrid();
    //grid->enableX(false);
    //QPen gridPen;
    //gridPen.setStyle(Qt::DotLine);
    //grid->setPen(QPen(GColor(CPLOTGRID)));
    //grid->attach(this);

    //timeCurves.resize(36);
    //for (int i = 0; i < 36; ++i) {
        //QColor color = QColor(255,255,255);
        //color.setHsv(60+i*(360/36), 255,255,255);

        //QPen pen = QPen(color);
        //pen.setWidth(3);

        //timeCurves[i] = new QwtPlotCurve();
        //timeCurves[i]->setPen(pen);
        //timeCurves[i]->setStyle(QwtPlotCurve::Lines);
        //timeCurves[i]->setRenderHint(QwtPlotItem::RenderAntialiased);
        //timeCurves[i]->attach(this);
        //QwtLegend *legend = new QwtLegend;
        //legend->setVisible(false);
        //legend->setDisabled(true);
        //timeCurves[i]->updateLegend(legend);
     //}
}

struct DataPoint {
    double time, hr, watts;
    int inter;
    DataPoint(double t, double h, double w, int i) :
        time(t), hr(h), watts(w), inter(i) {}
};

void
SmallPlot::recalc()
{
    if (!timeArray.size()) return;

    int rideTimeSecs = (long) ceil(timeArray[arrayLength - 1]);
    if (rideTimeSecs > 7*24*60*60) {
        QwtArray<double> data;
        wattsCurve->setData(data, data);
        hrCurve->setData(data, data);
        return;
    }

#if 0
    int nbpoints2 = (int)floor(rideTimeSecs/60/36)+2;
    //fprintf(stderr, "rideTimeSecs : %d, nbpoints2 : %d",rideTimeSecs/60, nbpoints2);

    QVector<double>datatime(nbpoints2);
    double *time[36];

    for (int i = 0; i < 36; ++i) {
        time[i]= new double[nbpoints2];
    }

    for (int secs = 0; secs < nbpoints2; ++secs) {
        datatime[secs] = 1;

        for (int i = 0; i < 36; ++i) {
            //fprintf(stderr, "\ni : %d, time : %d",i, secs + i*(nbpoints2-1));
            time[i][secs] =  secs + i*(nbpoints2-1);
        }
    }

    for (int i = 0; i < 36; ++i) {
    	timeCurves[i]->setData(time[i], datatime, nbpoints2);
    }
#endif

    double totalWatts = 0.0;
    double totalHr = 0.0;
    QList<DataPoint*> list;
    int i = 0;

    QVector<double> smoothWatts(rideTimeSecs + 1);
    QVector<double> smoothHr(rideTimeSecs + 1);
    QVector<double> smoothTime(rideTimeSecs + 1);

    QList<double> interList; //Just store the time that it happened.
                             //Intervals are sequential.

    int lastInterval = 0; //Detect if we hit a new interval

    for (int secs = 0; ((secs < smooth) && (secs < rideTimeSecs)); ++secs) {
        smoothWatts[secs] = 0.0;
        smoothHr[secs]    = 0.0;
    }
    for (int secs = smooth; secs <= rideTimeSecs; ++secs) {
        while ((i < arrayLength) && (timeArray[i] <= secs)) {
            DataPoint *dp =
                new DataPoint(timeArray[i], hrArray[i], wattsArray[i], interArray[i]);
            totalWatts += wattsArray[i];
            totalHr    += hrArray[i];
            list.append(dp);
            //Figure out when and if we have a new interval..
            if(lastInterval != interArray[i]) {
                lastInterval = interArray[i];
                interList.append(secs/60.0);
            }
            ++i;
        }
        while (!list.empty() && (list.front()->time < secs - smooth)) {
            DataPoint *dp = list.front();
            list.removeFirst();
            totalWatts -= dp->watts;
            totalHr    -= dp->hr;
            delete dp;
        }
        // TODO: this is wrong.  We should do a weighted average over the
        // seconds represented by each point...
        if (list.empty()) {
            smoothWatts[secs] = 0.0;
            smoothHr[secs]    = 0.0;
        }
        else {
            smoothWatts[secs]    = totalWatts / list.size();
            smoothHr[secs]       = totalHr / list.size();
        }
        smoothTime[secs]  = secs / 60.0;
    }
    wattsCurve->setData(smoothTime.constData(), smoothWatts.constData(), rideTimeSecs + 1);
    hrCurve->setData(smoothTime.constData(), smoothHr.constData(), rideTimeSecs + 1);
    setAxisScale(xBottom, 0.0, smoothTime[rideTimeSecs]);

    setYMax();


#if 0
    QString label[interList.size()];
    QwtText text[interList.size()];

    // arrays are safe since not passed
    // as a conyiguous array
    if (d_mrk) delete [] d_mrk;
    d_mrk = new QwtPlotMarker[interList.size()];
    for(int x = 0; x < interList.size(); x++) {
        // marker
        d_mrk[x].setValue(0,0);
        d_mrk[x].setLineStyle(QwtPlotMarker::VLine);
        d_mrk[x].setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
        d_mrk[x].setLinePen(QPen(Qt::black, 0, Qt::DashDotLine));
        d_mrk[x].attach(this);
        label[x].setNum(x+1);
        text[x] = QwtText(label[x]);
        text[x].setFont(QFont("Helvetica", 10, QFont::Bold));
        text[x].setColor(Qt::black);

        d_mrk[x].setValue(interList.at(x), 0.0);
        d_mrk[x].setLabel(text[x]);
    }
#endif

    replot();
}

void
SmallPlot::setYMax()
{
    double ymax = 0;
    QString ylabel = "";
    if (wattsCurve->isVisible()) {
        ymax = max(ymax, wattsCurve->maxYValue());
        ylabel += QString((ylabel == "") ? "" : " / ") + "Watts";
    }
    if (hrCurve->isVisible()) {
        ymax = max(ymax, hrCurve->maxYValue());
        ylabel += QString((ylabel == "") ? "" : " / ") + "BPM";
    }
    setAxisScale(yLeft, 0.0, ymax * 1.1);
    setAxisTitle(yLeft, ylabel);
    enableAxis(yLeft, false); // hide for a small plot
}

void
SmallPlot::setXTitle()
{
    setAxisTitle(xBottom, tr("Time (minutes)"));
}

void
SmallPlot::setAxisTitle(int axis, QString label)
{
    // setup the default fonts
    QFont stGiles; // hoho - Chart Font St. Giles ... ok you have to be British to get this joke
    stGiles.fromString(appsettings->value(this, GC_FONT_CHARTLABELS, QFont().toString()).toString());
    stGiles.setPointSize(appsettings->value(NULL, GC_FONT_CHARTLABELS_SIZE, 8).toInt());

    QwtText title(label);
    title.setFont(stGiles);
    QwtPlot::setAxisFont(axis, stGiles);
    QwtPlot::setAxisTitle(axis, title);
}

void
SmallPlot::setData(RideItem *rideItem)
{
    RideFile *ride = rideItem->ride();

    wattsArray.resize(ride->dataPoints().size());
    hrArray.resize(ride->dataPoints().size());
    timeArray.resize(ride->dataPoints().size());
    interArray.resize(ride->dataPoints().size());

    arrayLength = 0;
    foreach (const RideFilePoint *point, ride->dataPoints()) {
        timeArray[arrayLength]  = point->secs;
        wattsArray[arrayLength] = max(0, point->watts);
        hrArray[arrayLength]    = max(0, point->hr);
        interArray[arrayLength] = point->interval;
        ++arrayLength;
    }
    recalc();
}

void
SmallPlot::showPower(int state)
{
    assert(state != Qt::PartiallyChecked);
    wattsCurve->setVisible(state == Qt::Checked);
    setYMax();
    replot();
}

void
SmallPlot::showHr(int state)
{
    assert(state != Qt::PartiallyChecked);
    hrCurve->setVisible(state == Qt::Checked);
    setYMax();
    replot();
}

void
SmallPlot::setSmoothing(int value)
{
    smooth = value;
    recalc();
}
