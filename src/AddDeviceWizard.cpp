/*
 * Copyright (c) 2012 Mark Liversedge (liversedge@gmail.com)
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

#include "AddDeviceWizard.h"

// WIZARD FLOW
//
// 10. Select Device Type
// 20. Scan for Device / select Serial
// 30. Firmware for Fortius
// 50. Pair for ANT
// 60. Finalise
//

// Main wizard
AddDeviceWizard::AddDeviceWizard(MainWindow *main, DeviceConfiguration &here) : QWizard(main), main(main), here(here)
{
#ifdef Q_OS_MAC
    setWizardStyle(QWizard::ModernStyle);
#endif

    // delete when done
    setAttribute(Qt::WA_DeleteOnClose);

    setFixedHeight(500);
    setFixedWidth(550);

    // title
    setWindowTitle(tr("Add Device Wizard"));
    scanner = new DeviceScanner(this);

    setPage(10, new AddType(this));   // done
    setPage(20, new AddSearch(this)); // done
    setPage(30, new AddFirmware(this)); // done
    setPage(50, new AddPair(this));     // todo
    setPage(60, new AddFinal(this));    // todo -- including virtual power

    done = false;

    type = -1;
    current = 0;
    controller = NULL;

}

/*----------------------------------------------------------------------
 * Wizard Pages
 *--------------------------------------------------------------------*/

//Select device type
AddType::AddType(AddDeviceWizard *parent) : QWizardPage(parent), wizard(parent)
{
    setTitle(tr("Select Device"));
    setSubTitle(tr("What kind of device to add"));

    QVBoxLayout *layout = new QVBoxLayout;
    setLayout(layout);

    mapper = new QSignalMapper(this);
    connect(mapper, SIGNAL(mapped(QString)), this, SLOT(clicked(QString)));

    foreach(DeviceType t, wizard->deviceTypes.Supported) {
        if (t.type) {
            QCommandLinkButton *p = new QCommandLinkButton(t.name, t.description, this);
            connect(p, SIGNAL(clicked()), mapper, SLOT(map()));
            mapper->setMapping(p, QString("%1").arg(t.type));
            layout->addWidget(p);
        }
    }
    label = new QLabel("", this);
    layout->addWidget(label);

    next = 20;
    setFinalPage(false);
}

void
AddType::initializePage()
{
    // reset any device search info
    wizard->portSpec = "";
    wizard->found = false;
    wizard->current = 0;
    if (wizard->controller) {
        delete wizard->controller;
        wizard->controller = NULL;
    }
}
   

void
AddType::clicked(QString p)
{
    // reset -- particularly since we might get here from
    //          other pages hitting 'Back'
    initializePage();
    wizard->type = p.toInt();

    // what are we scanning for?
    int i=0;
    foreach(DeviceType t, wizard->deviceTypes.Supported) {
        if (t.type == wizard->type) wizard->current = i;
        i++;
    }

    wizard->found = wizard->scanner->quickScan(false); // do a quick scan

    // Still no dice. Go to the not found dialog
    if (wizard->found == false) next =20;
    else {
        switch(wizard->deviceTypes.Supported[wizard->current].type) {
        case DEV_ANTLOCAL : next = 50; break; // pair 
        default:
        case DEV_CT : next = 60; break; // confirm and add 
        case DEV_FORTIUS : next = 30; break; // confirm and add 
        }
    }
    wizard->next();
}

DeviceScanner::DeviceScanner(AddDeviceWizard *wizard) : wizard(wizard) {}

void
DeviceScanner::run()
{
    active = true;
    bool result = false;
    for (int i=0; active && !result && i<50; i++) { // search for longer

        // better to wait a while, esp. if its just a USB device
#ifdef WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
        result = quickScan(false);
    }
    if (active) emit finished(result); // only signal if we weren't aborted!
}

void
DeviceScanner::stop()
{
    active = false;
}


bool
DeviceScanner::quickScan(bool deep) // scan quickly or if true scan forever, as deep as possible
                                       // for now deep just means try 3 time before giving up, but we
                                       // may want to change that to include scanning more devices?
{
    // get controller
    if (wizard->controller) {
        delete wizard->controller;
        wizard->controller=NULL;
    }

    switch (wizard->deviceTypes.Supported[wizard->current].type) {

    // we will need a factory for this soon..
    case DEV_ANTPLUS : wizard->controller = new ANTplusController(NULL, NULL); break;
    case DEV_CT : wizard->controller = new ComputrainerController(NULL, NULL); break;
#ifdef GC_HAVE_LIBUSB
    case DEV_FORTIUS : wizard->controller = new FortiusController(NULL, NULL); break;
#endif
    case DEV_NULL : wizard->controller = new NullController(NULL, NULL); break;
    case DEV_ANTLOCAL : wizard->controller = new ANTlocalController(NULL, NULL); break;

    default: wizard->controller = NULL; break;

    }


    //----------------------------------------------------------------------
    // Search for USB devices
    //----------------------------------------------------------------------

    bool isfound = false;
    int count=0;
    do {

        // can we find it automatically?
        isfound = wizard->controller->find();

        if (isfound == false && (wizard->deviceTypes.Supported[wizard->current].connector == DEV_LIBUSB ||
                            wizard->deviceTypes.Supported[wizard->current].connector == DEV_USB)) {

            // Go to next page where we do not found, rescan and manual override
            if (!deep) return false;
        }

    
        //----------------------------------------------------------------------
        // Search serial ports
        //----------------------------------------------------------------------

        if (isfound == false && wizard->deviceTypes.Supported[wizard->current].connector == DEV_SERIAL) {

            // automatically discover a serial port ...
            QString error;
            foreach (CommPortPtr port, Serial::myListCommPorts(error)) {

                // check if controller still exists. gets deleted when scan cancelled
                if (wizard->controller && wizard->controller->discover(port->name()) == true) {
                    isfound = true;
                    wizard->portSpec = port->name();
                    break;
                }
            }

            // if we still didn't find it then we need to fall back to the user
            // specifying the device on the next page
        }

    } while (!isfound && deep && count++ < 2);

    return isfound;

}

// Scan for device port / usb etc
AddSearch::AddSearch(AddDeviceWizard *parent) : QWizardPage(parent), wizard(parent)
{
    setSubTitle(tr("Scan for connected devices"));

    QVBoxLayout *layout = new QVBoxLayout;
    setLayout(layout);

    isSearching = false;
    active = false;


    label = new QLabel("Please make sure your device is connected, switched on and working. "
                               "We will scan for the device type you have selected at known ports.\n\n");
    label->setWordWrap(true);
    layout->addWidget(label);

    bar = new QProgressBar(this);
    bar->setMaximum(100);
    bar->setMinimum(0);
    bar->setValue(0);
    //bar->setText("Searching...");
    layout->addWidget(bar);

    QHBoxLayout *hlayout2 = new QHBoxLayout;
    stop = new QPushButton("Search", this);
    hlayout2->addStretch();
    hlayout2->addWidget(stop);
    layout->addLayout(hlayout2);

    label1 = new QLabel("If your device is not found you can select the device port "
                               "manually by using the selection box below."); 
    label1->setWordWrap(true);
    layout->addWidget(label1);

    label2 = new QLabel("\nDevice found.\nClick Next to Continue\n");
    label2->hide();
    label2->setWordWrap(true);
    layout->addWidget(label2);

    QHBoxLayout *hlayout = new QHBoxLayout;
    manual = new QComboBox(this);
    hlayout->addStretch();
    hlayout->addWidget(manual);
    layout->addLayout(hlayout);

    layout->addStretch();

    connect(stop, SIGNAL(clicked()), this, SLOT(doScan()));
    connect(manual, SIGNAL(currentIndexChanged(int)), this, SLOT(chooseCOMPort()));
    connect(wizard->scanner, SIGNAL(finished(bool)), this, SLOT(scanFinished(bool)));

}

void
AddSearch::chooseCOMPort()
{
    if (active) return;

    if (manual->currentIndex() <= 0) { // we unselected or something.
        wizard->found = false;
        wizard->portSpec = "";
        return;
    }

    // stop any scan that may be in process?
    if (isSearching == true) {
       doScan(); // remember doScan toggles with the stop/search again button
    }

    // let the user select the port
    wizard->portSpec = manual->itemText(manual->currentIndex());

    // carry on then 
    wizard->found = true; // ugh
}

void
AddSearch::initializePage()
{
    active = true;
    setTitle(QString(tr("%1 Search")).arg(wizard->deviceTypes.Supported[wizard->current].name));

    // we only ask for the device file if it is a serial device
    if (wizard->deviceTypes.Supported[wizard->current].connector == DEV_SERIAL) {

        // wipe away whatever items it has now
        for (int i=manual->count(); i > 0; i--) manual->removeItem(0);

        // add in the items we have..
        manual->addItem("Select COM port");
        QString error;
        foreach (CommPortPtr port, Serial::myListCommPorts(error)) manual->addItem(port->name());
        manual->show();
        label1->show();


    } else {
        label1->hide();
        manual->hide();
    }

    bar->show();
    stop->show();
    label->show();
    label2->hide();
    active = false;
    doScan();
}

void
AddSearch::scanFinished(bool result)
{
    isSearching = false;
    wizard->found = result;
    bar->setMaximum(100);
    bar->setMinimum(0);
    bar->setValue(0);
    stop->setText("Search again");

    if (result == true) { // woohoo we found one
        bar->hide();
        stop->hide();
        manual->hide();
        label->hide();
        label1->hide();
        if (wizard->portSpec != "")
            label2->setText(QString("\nDevice found on %1.\nPress Next to Continue\n").arg(wizard->portSpec));
        else
            label2->setText("\nDevice found.\nPress Next to Continue\n");
        label2->show();
    } 
    QApplication::processEvents();
    emit completeChanged();
}

void
AddSearch::doScan()
{
    if (isSearching == false) { // start a scan

        // make bar bouncy...
        bar->setMaximum(0);
        bar->setMinimum(0);
        bar->setValue(0);
        stop->setText("Stop Searching");
        isSearching = true;
        manual->setCurrentIndex(0); //deselect any chosen port
        wizard->found = false;
        wizard->portSpec = "";

        wizard->scanner->start();

    } else { // stop a scan

        isSearching = false;
        // make bar stationary...
        bar->setMaximum(100);
        bar->setMinimum(0);
        bar->setValue(0);
        stop->setText("Search again");

        wizard->scanner->stop();
    }
}

int
AddSearch::nextId() const
{
    // Still no dice. Go to the not found dialog
    if (wizard->found == false)  return 60;
    else {
        switch(wizard->deviceTypes.Supported[wizard->current].type) {
        case DEV_ANTLOCAL : return 50; break; // pair 
        default:
        case DEV_CT : return 60; break; // confirm and add 
        case DEV_FORTIUS : return 30; break; // confirm and add 
        }
    }
}

bool
AddSearch::validatePage()
{
    return wizard->found;
}

void
AddSearch::cleanupPage()
{
    wizard->scanner->stop();
    if (isSearching) {
    // give it time to stop...
#ifdef WIN32
        Sleep(2000);
#else
        sleep(2);
#endif
    }
    isSearching=false;
    if (wizard->controller) {
        delete wizard->controller;
        wizard->controller = NULL;
    }
}

// Fortius Firmware
AddFirmware::AddFirmware(AddDeviceWizard *parent) : QWizardPage(parent), wizard(parent)
{
    setTitle(tr("Select Firmware"));
    setSubTitle(tr("Select Firmware for Tacx Fortius"));

    // create widgets
    browse = new QPushButton("Browse", this);
    copy = new QCheckBox("Copy to Library");
    copy->setChecked(true);

    help = new QLabel(this);
    help->setWordWrap(true);
    help->setText("Tacx Fortius trainers require a firmware file "
                  "which is provided by Tacx BV. This file is a "
                  "copyrighted file and cannot be distributed with "
                  "GoldenCheetah.\n\n"
                  "On windows it is typically installed in C:\\Windows\\system32 "
                  "and is called 'FortiusSWPID1942Renum.hex'.\n\n"
#if defined Q_OS_LINUX || defined Q_OS_MAC
                  "On Linux and Apple computers you will need to "
                  "extract it from the VR Software CD."
                  "The file we need is within the 'data2.cab' file, "
                  "which is an InstallShield file that can be read "
                  "with the 'unshield' tool\n\n"
#endif
                  "Please take care to ensure that the file is the latest version "
                  "of the Firmware file.\n\n"
                  "If you choose to copy to library the file will be copied into the "
                  "GoldenCheetah library, otherwise we will reference it. ");

    file = new QLabel("File:", this);

    name= new QLineEdit(this);
    name->setEnabled(false);

    QString fortiusFirmware = appsettings->value(this, FORTIUS_FIRMWARE, "").toString();
    name->setText(fortiusFirmware);

    // Layout widgets
    QHBoxLayout *buttons = new QHBoxLayout;
    QHBoxLayout *filedetails = new QHBoxLayout;
    filedetails->addWidget(file);
    filedetails->addWidget(name);
    filedetails->addWidget(browse);
    filedetails->addStretch();

    buttons->addWidget(copy);
    buttons->addStretch();

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(filedetails);
    mainLayout->addWidget(help);
    mainLayout->addStretch();
    mainLayout->addLayout(buttons);

    // connect widgets
    connect(browse, SIGNAL(clicked()), this, SLOT(browseClicked()));
}

bool
AddFirmware::validatePage()
{
    QString filePath = name->text();
    if (filePath == "" || !QFile(filePath).exists()) return false;

    // either copy it, or reference it!
    if (copy->isChecked()) {

        QString fileName = QFileInfo(filePath).fileName();
        QString targetFileName = QFileInfo(mainWindow->home.absolutePath() + "/../").absolutePath() + "/" + fileName;

        // check not the same thing!
        if(QFileInfo(fileName).absolutePath() != QFileInfo(targetFileName).absolutePath()) {
            // if the current file exists, wipe it
            if (QFile(targetFileName).exists()) QFile(targetFileName).remove();
            QFile(filePath).copy(targetFileName);
        }
        name->setText(targetFileName);
    }
    appsettings->setValue(FORTIUS_FIRMWARE, name->text());
    return true;
}

void
AddFirmware::browseClicked()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("Intel Firmware File (*.hex)"));
    if (file != "") name->setText(file);
}

// Pair devices
AddPair::AddPair(AddDeviceWizard *parent) : QWizardPage(parent), wizard(parent)
{
    setTitle(tr("Pair Devices"));
    setSubTitle(tr("Search for and pair ANT+ devices"));

    signalMapper = NULL;

    QVBoxLayout *layout = new QVBoxLayout;
    setLayout(layout);

    channelWidget = new QTreeWidget(this);
    layout->addWidget(channelWidget);
}

static void
addSensorTypes(ANT *ant, QComboBox *p)
{
    for (int i=0; ant->ant_sensor_types[i].suffix !=  '\0'; i++) {
        if (*ant->ant_sensor_types[i].iconname != '\0') {
            p->addItem(QIcon(ant->ant_sensor_types[i].iconname), ant->ant_sensor_types[i].descriptive_name, ant->ant_sensor_types[i].type);
        } else {
            p->addItem(ant->ant_sensor_types[i].descriptive_name, ant->ant_sensor_types[i].type);
        }
    }
}

void
AddPair::cleanupPage()
{
    updateValues.stop();
    if (wizard->controller) {
        wizard->controller->stop();
#ifdef WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
        delete wizard->controller;
        wizard->controller = NULL;
    }
}

static void enableDisable(QTreeWidget *tree)
{
    // enable disable widgets based upon sensor selection
    for (int i=0; i< tree->invisibleRootItem()->childCount(); i++) {
        QTreeWidgetItem *item = tree->invisibleRootItem()->child(i);

        // is it selected or not?
        bool enable = (dynamic_cast<QComboBox*>(tree->itemWidget(item,0))->currentIndex() != 0);

        // enable all thos widgetry
        tree->itemWidget(item,2)->setEnabled(enable); // value
        tree->itemWidget(item,3)->setEnabled(enable); // status
    }
}

void
AddPair::initializePage()
{
    // setup the controller and start it off so we can
    // manipulate it
    if (wizard->controller) delete wizard->controller;
    if (signalMapper) delete signalMapper;
    wizard->controller = new ANTlocalController(NULL,NULL);
    dynamic_cast<ANTlocalController*>(wizard->controller)->setDevice(wizard->portSpec);
    dynamic_cast<ANTlocalController*>(wizard->controller)->myANTlocal->setConfigurationMode(true); //XXX
    wizard->controller->start();
    wizard->profile=""; // clear any thing thats there now
    signalMapper = new QSignalMapper(this);

    // Channel 0, look for any (0 devicenumber) speed and distance device

    // wait for it to start
#ifdef WIN32
    Sleep(1000);
#else
    sleep(1);
#endif
    int channels = dynamic_cast<ANTlocalController*>(wizard->controller)->channels();

    // Tree Widget of the channel controls
    channelWidget->clear();
    channelWidget->headerItem()->setText(0, tr("Sensor"));
    channelWidget->headerItem()->setText(1, tr("ANT+ Id"));
    channelWidget->headerItem()->setText(2, tr("Value"));
    channelWidget->headerItem()->setText(3, tr("Status"));
    channelWidget->setColumnCount(4);
    channelWidget->setSelectionMode(QAbstractItemView::NoSelection);
    //channelWidget->setEditTriggers(QAbstractItemView::SelectedClicked); // allow edit
    channelWidget->setUniformRowHeights(true);
    channelWidget->setIndentation(0);

    channelWidget->header()->resizeSection(0,175); // type 
    channelWidget->header()->resizeSection(1,75); // id
    channelWidget->header()->resizeSection(2,120); // value
    channelWidget->header()->resizeSection(3,110); // status

    // defaults
    static const int index4[4] = { 1,2,3,5 };
    static const int index8[8] = { 1,2,3,4,5,0,0,0 };
    const int *index = channels == 4 ? index4 : index8;

    // how many devices we got then?
    for (int i=0; i< channels; i++) {

        QTreeWidgetItem *add = new QTreeWidgetItem(channelWidget->invisibleRootItem());
        add->setFlags(add->flags() | Qt::ItemIsEditable);

        // sensor type
        QComboBox *sensorSelector = new QComboBox(this);
        addSensorTypes(dynamic_cast<ANTlocalController*>(wizard->controller)->myANTlocal, sensorSelector);
        sensorSelector->setCurrentIndex(index[i]);
        channelWidget->setItemWidget(add, 0, sensorSelector);

        // sensor id
        QLineEdit *sensorId = new QLineEdit(this);
        sensorId->setEnabled(false);
        sensorId->setText("none");
        channelWidget->setItemWidget(add, 1, sensorId);

        // value
        QLabel *value = new QLabel(this);
        QFont bigger;
        bigger.setPointSize(25);
        value->setFont(bigger);
        value->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);
        value->setText("0");
        channelWidget->setItemWidget(add, 2, value);

        // status
        QLabel *status = new QLabel(this);
        status->setText("Un-Paired");
        channelWidget->setItemWidget(add, 3, status);

        //channelWidget->verticalHeader()->resizeSection(i,40)
        connect(sensorSelector, SIGNAL(currentIndexChanged(int)), signalMapper, SLOT(map()));
        signalMapper->setMapping(sensorSelector, i);
    }
    channelWidget->setCurrentItem(channelWidget->invisibleRootItem()->child(0));
    enableDisable(channelWidget);

    updateValues.start(200); // 5hz
    connect(signalMapper, SIGNAL(mapped(int)), this, SLOT(sensorChanged(int)));
    connect(&updateValues, SIGNAL(timeout()), this, SLOT(getChannelValues()));
    connect(wizard->controller, SIGNAL(foundDevice(int,int,int)), this, SLOT(channelInfo(int,int,int)));
    connect(wizard->controller, SIGNAL(searchTimeout(int)), this, SLOT(searchTimeout(int)));
    //connect(wizard->controller, SIGNAL(lostDevice(int)), this, SLOT(searchTimeout(int)));

    // now we're ready to get notifications - set channels
    for (int i=0; i<channels; i++) sensorChanged(i);

}

void
AddPair::sensorChanged(int channel)
{
    QTreeWidgetItem *item = channelWidget->invisibleRootItem()->child(channel);
    enableDisable(channelWidget);

    // first off lets unassign this channel
    dynamic_cast<ANTlocalController*>(wizard->controller)->myANTlocal->setChannel(channel, -1, 0);
    dynamic_cast<QLineEdit*>(channelWidget->itemWidget(item,1))->setText("none");
    dynamic_cast<QLabel*>(channelWidget->itemWidget(item,2))->setText(0);

    // what is it then? unused or restart scan?
    QComboBox *p = dynamic_cast<QComboBox *>(channelWidget->itemWidget(item,0));
    int channel_type = p->itemData(p->currentIndex()).toInt();
    if (channel_type == ANTChannel::CHANNEL_TYPE_UNUSED) {
        dynamic_cast<QLabel*>(channelWidget->itemWidget(item,3))->setText("Unused");
    } else {
        dynamic_cast<QLabel*>(channelWidget->itemWidget(item,3))->setText("Searching...");
    dynamic_cast<ANTlocalController*>(wizard->controller)->myANTlocal->setChannel(channel, 0, channel_type);
    }
}

void
AddPair::channelInfo(int channel, int device_number, int device_id)
{
    Q_UNUSED(device_id);
    QTreeWidgetItem *item = channelWidget->invisibleRootItem()->child(channel);
    dynamic_cast<QLineEdit *>(channelWidget->itemWidget(item,1))->setText(QString("%1").arg(device_number));
    dynamic_cast<QLabel *>(channelWidget->itemWidget(item,3))->setText(QString("Paired"));
}

void
AddPair::searchTimeout(int channel)
{
    // Kick if off again, just mimic user reselecting the same sensor type
    sensorChanged(channel);
}


void 
AddPair::getChannelValues()
{
    if (wizard->controller == NULL) return;

    // enable disable widgets based upon sensor selection
    for (int i=0; i< channelWidget->invisibleRootItem()->childCount(); i++) {
        QTreeWidgetItem *item = channelWidget->invisibleRootItem()->child(i);

        // is it selected or not?
        bool enable = (dynamic_cast<QComboBox*>(channelWidget->itemWidget(item,0))->currentIndex() != 0);

        if (enable) {
            QComboBox *p =dynamic_cast<QComboBox*>(channelWidget->itemWidget(item,0));

            // speed+cadence is two values!
            if (p->itemData(p->currentIndex()) == ANTChannel::CHANNEL_TYPE_SandC) {
            dynamic_cast<QLabel *>(channelWidget->itemWidget(item,2))->setText(QString("%1 %2")
                .arg((int)dynamic_cast<ANTlocalController*>(wizard->controller)->myANTlocal->channelValue2(i) //speed
                          * (appsettings->value(NULL, GC_WHEELSIZE, 2100).toInt()/1000) * 60 / 1000)
                .arg((int)dynamic_cast<ANTlocalController*>(wizard->controller)->myANTlocal->channelValue(i))); // cad
            } else {
            dynamic_cast<QLabel *>(channelWidget->itemWidget(item,2))->setText(QString("%1")
                .arg((int)dynamic_cast<ANTlocalController*>(wizard->controller)->myANTlocal->channelValue(i)));
            }
        }
    }
    
}

bool
AddPair::validatePage()
{
    // when next is clicked we need to get the paired values
    // and create a profile, a blank profile will be created if
    // no devices have been paired. This means devices will be
    // automatically paired at runtime
    wizard->profile="";
    for (int i=0; i< channelWidget->invisibleRootItem()->childCount(); i++) {
        QTreeWidgetItem *item = channelWidget->invisibleRootItem()->child(i);

        // what is it then? unused or restart scan?
        QComboBox *p = dynamic_cast<QComboBox *>(channelWidget->itemWidget(item,0));
        int channel_type = p->itemData(p->currentIndex()).toInt();

        if (channel_type == ANTChannel::CHANNEL_TYPE_UNUSED) continue; // not paired

        int device_number = dynamic_cast<QLineEdit*>(channelWidget->itemWidget(item,1))->text().toInt();

        if (device_number)
            wizard->profile += QString(wizard->profile != "" ? ", %1%2" : "%1%2")
                               .arg(device_number)
                               .arg(ANT::deviceIdCode(channel_type));
    }
    return true;
}

// Final confirmation
AddFinal::AddFinal(AddDeviceWizard *parent) : QWizardPage(parent), wizard(parent)
{
    setTitle(tr("Done"));
    setSubTitle(tr("Confirm configuration and add device"));

    QVBoxLayout *layout = new QVBoxLayout;
    setLayout(layout);

    QLabel *label = new QLabel("We will now add a new device with the configuration shown "
                               "below. Please take a moment to review and then click Finish "
                               "to add the device and complete this wizard, or press the Back "
                               "button to make amendments.\n\n");
    label->setWordWrap(true);
    layout->addWidget(label);

    QHBoxLayout *hlayout = new QHBoxLayout;
    layout->addLayout(hlayout);

    QFormLayout *formlayout = new QFormLayout;
    formlayout->addRow(new QLabel("Name*", this), (name=new QLineEdit(this)));
    formlayout->addRow(new QLabel("Port", this), (port=new QLineEdit(this)));
    formlayout->addRow(new QLabel("Profile", this), (profile=new QLineEdit(this)));
    formlayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    //profile->setFixedWidth(200);
    port->setFixedWidth(150);
    port->setEnabled(false); // no edit
    //name->setFixedWidth(230);
    hlayout->addLayout(formlayout);

    QFormLayout *form2layout = new QFormLayout;
    form2layout->addRow(new QLabel("Virtual", this), (virtualPower=new QComboBox(this)));
    form2layout->addRow(new QLabel("Wheel Size", this), (wheelSize=new QComboBox(this)));
    // XXX NOTE: THESE MUST CORRESPOND TO THE CODE
    //           IN RealtimeController.cpp WHICH
    //           POST-PROCESSES INBOUND TELEMETRY
    virtualPower->addItem("None");
    virtualPower->addItem("Power - Kurt Kinetic Cyclone");
    virtualPower->addItem("Power - Kurt Kinetic Road Machine");
    virtualPower->addItem("Power - Cyclops Fluid 2");
    virtualPower->addItem("Power - BT Advanced Training System");
    virtualPower->addItem("Power - LeMond Revolution");
    virtualPower->addItem("Power - 1UP USA Trainer");
    virtualPower->addItem("Power - Minoura V100 Trainer (H)");
    virtualPower->addItem("Power - Minoura V100 Trainer (5)");
    virtualPower->addItem("Power - Minoura V100 Trainer (4)");
    virtualPower->addItem("Power - Minoura V100 Trainer (3)");
    virtualPower->addItem("Power - Minoura V100 Trainer (2)");
    virtualPower->addItem("Power - Minoura V100 Trainer (1)");
    virtualPower->addItem("Power - Minoura V100 Trainer (L)");

    wheelSize->addItem("Road/Cross (700C/622)"); // 2100mm
    wheelSize->addItem("Tri/TT (650C)"); // 1960mm
    wheelSize->addItem("Mountain (26\")"); // 1985mm
    wheelSize->addItem("BMX (20\")"); // 1750mm

    hlayout->addLayout(form2layout);
    layout->addStretch();

    selectDefault = new QGroupBox("Selected by default", this);
    selectDefault->setCheckable(true);
    selectDefault->setChecked(false);
    layout->addWidget(selectDefault);

    QGridLayout *grid = new QGridLayout;
    selectDefault->setLayout(grid);
    grid->addWidget((defWatts=new QCheckBox("Power")), 0,0, Qt::AlignVCenter|Qt::AlignLeft);
    grid->addWidget((defBPM=new QCheckBox("Heartrate")), 1,0, Qt::AlignVCenter|Qt::AlignLeft);
    grid->addWidget((defKPH=new QCheckBox("Speed")), 0,1, Qt::AlignVCenter|Qt::AlignLeft);
    grid->addWidget((defRPM=new QCheckBox("Cadence")), 1,1, Qt::AlignVCenter|Qt::AlignLeft);
    layout->addStretch();
}

void
AddFinal::initializePage()
{
    port->setText(wizard->portSpec);
    profile->setText(wizard->profile);
    virtualPower->setCurrentIndex(0);
}

bool
AddFinal::validatePage()
{
    if (name->text() != "") {

        // lets update 'here' with what we did then...
        wizard->here.type = wizard->type;
        wizard->here.name = name->text();
        wizard->here.portSpec = port->text();
        wizard->here.deviceProfile = profile->text();
        wizard->here.defaultString = QString(defWatts->isChecked() ? "P" : "") +
                                     QString(defBPM->isChecked() ? "H" : "") +
                                     QString(defRPM->isChecked() ? "C" : "") +
                                     QString(defKPH->isChecked() ? "S" : "");
        wizard->here.postProcess = virtualPower->currentIndex();

        switch (wheelSize->currentIndex()) {

            default:
            case 0: wizard->here.wheelSize = 2100 ; break;
            case 1: wizard->here.wheelSize = 1960 ; break;
            case 2: wizard->here.wheelSize = 1985 ; break;
            case 3: wizard->here.wheelSize = 1750 ; break;
        }

        // shut down the controller, if it is there, since it will
        // still be connected to the device (in case we hit the back button)
        if (wizard->controller) {
            wizard->controller->stop();
#ifdef WIN32
            Sleep(1000);
#else
            sleep(1);
#endif
            delete wizard->controller;
            wizard->controller = NULL;
        }
        return true;
    }
    return false;
}
