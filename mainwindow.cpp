#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "programmer.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>

static Programmer *p;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    p = new Programmer();
    ui->setupUi(this);
    ui->pages->setCurrentWidget(ui->notConnectedPage);
    ui->actionUpdate_firmware->setEnabled(false);

    // Fill in the list of SIMM chip capacities (should support chips ranging from 128KB to 512KB in size (= 1, 2, or 4 Mb)
    ui->simmCapacityBox->addItem("128 KB (1 Mb) per chip * 4 chips = 512 KB", QVariant(512 * 1024));
    ui->simmCapacityBox->addItem("256 KB (2 Mb) per chip * 4 chips = 1 MB", QVariant(1 * 1024 * 1024));
    ui->simmCapacityBox->addItem("512 KB (4 Mb) per chip * 4 chips = 2 MB", QVariant(2 * 1024 * 1024));

    ui->chosenWriteFile->setText("");
    ui->chosenReadFile->setText("");
    writeFileValid = false;
    readFileValid = false;
    ui->writeToSIMMButton->setEnabled(false);
    ui->readFromSIMMButton->setEnabled(false);
    ui->progressBar->setValue(0);
    ui->progressBar->setEnabled(false);
    ui->statusLabel->setText("");
    ui->cancelButton->setEnabled(false);

    readFile = NULL;
    writeFile = NULL;
    verifyArray = NULL;
    verifyBuffer = NULL;

    connect(p, SIGNAL(writeStatusChanged(WriteStatus)), SLOT(programmerWriteStatusChanged(WriteStatus)));
    connect(p, SIGNAL(writeTotalLengthChanged(uint32_t)), SLOT(programmerWriteTotalLengthChanged(uint32_t)));
    connect(p, SIGNAL(writeCompletionLengthChanged(uint32_t)), SLOT(programmerWriteCompletionLengthChanged(uint32_t)));
    connect(p, SIGNAL(electricalTestStatusChanged(ElectricalTestStatus)), SLOT(programmerElectricalTestStatusChanged(ElectricalTestStatus)));
    connect(p, SIGNAL(electricalTestFailLocation(uint8_t,uint8_t)), SLOT(programmerElectricalTestLocation(uint8_t,uint8_t)));
    connect(p, SIGNAL(readStatusChanged(ReadStatus)), SLOT(programmerReadStatusChanged(ReadStatus)));
    connect(p, SIGNAL(readTotalLengthChanged(uint32_t)), SLOT(programmerReadTotalLengthChanged(uint32_t)));
    connect(p, SIGNAL(readCompletionLengthChanged(uint32_t)), SLOT(programmerReadCompletionLengthChanged(uint32_t)));
    connect(p, SIGNAL(identificationStatusChanged(IdentificationStatus)), SLOT(programmerIdentifyStatusChanged(IdentificationStatus)));
    connect(p, SIGNAL(firmwareFlashStatusChanged(FirmwareFlashStatus)), SLOT(programmerFirmwareFlashStatusChanged(FirmwareFlashStatus)));
    connect(p, SIGNAL(firmwareFlashTotalLengthChanged(uint32_t)), SLOT(programmerFirmwareFlashTotalLengthChanged(uint32_t)));
    connect(p, SIGNAL(firmwareFlashCompletionLengthChanged(uint32_t)), SLOT(programmerFirmwareFlashCompletionLengthChanged(uint32_t)));
    connect(p, SIGNAL(programmerBoardConnected()), SLOT(programmerBoardConnected()));
    connect(p, SIGNAL(programmerBoardDisconnected()), SLOT(programmerBoardDisconnected()));
    connect(p, SIGNAL(programmerBoardDisconnectedDuringOperation()), SLOT(programmerBoardDisconnectedDuringOperation()));
    p->startCheckingPorts();

    /*QList<QextPortInfo> l = QextSerialEnumerator::getPorts();
    foreach (QextPortInfo p, l)
    {
        qDebug() << "Found port...";
        qDebug() << "Enum name:" << p.enumName;
        qDebug() << "Friend name:" << p.friendName;
        qDebug() << "Phys name:" << p.physName;
        qDebug() << "Port name:" << p.portName;
        qDebug() << "Product ID:" << hex << p.productID;
        qDebug() << "Vendor ID:" << hex << p.vendorID;
        qDebug() << "";

#ifdef Q_WS_WIN
        if (p.portName.startsWith("COM"))
#endif
        {
            // TODO: Do checking for valid USB ID?

            if ((p.vendorID == 0x03EB) && (p.productID == 0x204B))
            {
                ui->portList->addItem(QString("%1 (Programmer)").arg(p.portName), QVariant(p.portName));
            }
            else
            {
                ui->portList->addItem(p.portName, QVariant(p.portName));
            }
        }
    }*/
}

MainWindow::~MainWindow()
{
    delete p;
    delete ui;
}

void MainWindow::on_selectWriteFileButton_clicked()
{
    QString filename = QFileDialog::getOpenFileName(this, "Select a ROM image:");
    if (!filename.isNull())
    {
        writeFileValid = true;
        ui->chosenWriteFile->setText(filename);
        ui->writeToSIMMButton->setEnabled(true);
    }
}

void MainWindow::on_selectReadFileButton_clicked()
{
    QString filename = QFileDialog::getSaveFileName(this, "Save ROM image as:");
    if (!filename.isNull())
    {
        readFileValid = true;
        ui->chosenReadFile->setText(filename);
        ui->readFromSIMMButton->setEnabled(true);
    }
}

void MainWindow::on_readFromSIMMButton_clicked()
{
    // We are not doing a verify after a write..
    readVerifying = false;
    if (readFile)
    {
        readFile->close();
        delete readFile;
    }
    readFile = new QFile(ui->chosenReadFile->text());
    if (readFile)
    {
        if (!readFile->open(QFile::WriteOnly))
        {
            delete readFile;
            readFile = NULL;
            programmerReadStatusChanged(ReadError);
            return;
        }
        resetAndShowStatusPage();
        p->readSIMM(readFile);
        qDebug() << "Reading from SIMM...";
    }
    else
    {
        programmerReadStatusChanged(ReadError);
    }
}

void MainWindow::on_writeToSIMMButton_clicked()
{
    if (writeFile)
    {
        writeFile->close();
        delete writeFile;
    }
    writeFile = new QFile(ui->chosenWriteFile->text());
    if (writeFile)
    {
        if (!writeFile->open(QFile::ReadOnly))
        {
            delete writeFile;
            writeFile = NULL;
            programmerWriteStatusChanged(WriteError);
            return;
        }
        resetAndShowStatusPage();
        p->writeToSIMM(writeFile);
        qDebug() << "Writing to SIMM...";
    }
    else
    {
        programmerWriteStatusChanged(WriteError);
    }
}

void MainWindow::on_chosenWriteFile_textEdited(const QString &newText)
{
    QFileInfo fi(newText);
    if (fi.exists() && fi.isFile())
    {
        ui->writeToSIMMButton->setEnabled(true);
        writeFileValid = true;
    }
    else
    {
        ui->writeToSIMMButton->setEnabled(false);
        writeFileValid = false;
    }
}

void MainWindow::programmerWriteStatusChanged(WriteStatus newStatus)
{
    switch (newStatus)
    {
    case WriteErasing:
        ui->statusLabel->setText("Erasing SIMM (this may take a few seconds)...");
        break;
    case WriteComplete:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        if (ui->verifyAfterWriteBox->isChecked())
        {
            // Set up the read buffers
            readVerifying = true;
            if (verifyArray) delete verifyArray;
            verifyArray = new QByteArray((int)p->SIMMCapacity(), 0);
            if (verifyBuffer) delete verifyBuffer;
            verifyBuffer = new QBuffer(verifyArray);
            if (!verifyBuffer->open(QBuffer::ReadWrite))
            {
                delete verifyBuffer;
                delete verifyArray;
                verifyBuffer = NULL;
                verifyArray = NULL;
                programmerReadStatusChanged(ReadError);
                return;
            }

            // Start reading from SIMM to temporary RAM buffer
            resetAndShowStatusPage();
            p->readSIMM(verifyBuffer);
            qDebug() << "Reading from SIMM for verification...";
        }
        else
        {
            QMessageBox::information(this, "Write complete", "The write operation finished.");
            ui->pages->setCurrentWidget(ui->controlPage);
        }
        break;
    case WriteError:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Write error", "An error occurred writing to the SIMM.");
        break;
    case WriteCancelled:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Write cancelled", "The write operation was cancelled.");
        break;
    case WriteEraseComplete:
        ui->statusLabel->setText("Writing SIMM...");
        break;
    case WriteEraseFailed:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Write error", "An error occurred erasing the SIMM.");
        break;
    case WriteTimedOut:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Write timed out", "The write operation timed out.");
        break;
    case WriteFileTooBig:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "File too big", "The file you chose to write to the SIMM is too big according to the chip size you have selected.");
        break;
    }
}

void MainWindow::programmerWriteTotalLengthChanged(uint32_t totalLen)
{
    ui->progressBar->setMaximum((int)totalLen);
}

void MainWindow::programmerWriteCompletionLengthChanged(uint32_t len)
{
    ui->progressBar->setValue((int)len);
}



void MainWindow::on_electricalTestButton_clicked()
{
    resetAndShowStatusPage();
    electricalTestString = "";
    p->runElectricalTest();
}

void MainWindow::programmerElectricalTestStatusChanged(ElectricalTestStatus newStatus)
{
    switch (newStatus)
    {
    case ElectricalTestStarted:
        ui->statusLabel->setText("Running electrical test (this may take a few seconds)...");
        qDebug() << "Electrical test started";
        break;
    case ElectricalTestPassed:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::information(this, "Test passed", "The electrical test passed successfully.");
        break;
    case ElectricalTestFailed:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Test failed", "The electrical test failed:\n\n" + electricalTestString);
        break;
    case ElectricalTestTimedOut:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Test timed out", "The electrical test operation timed out.");
        break;
    case ElectricalTestCouldntStart:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Communication error", "Unable to communicate with programmer board.");
        break;
    }
}

void MainWindow::programmerElectricalTestLocation(uint8_t loc1, uint8_t loc2)
{
    //qDebug() << "Electrical test error at (" << p->electricalTestPinName(loc1) << "," << p->electricalTestPinName(loc2) << ")";
    if (electricalTestString != "")
    {
        electricalTestString.append("\n");
    }

    electricalTestString.append(p->electricalTestPinName(loc1));
    electricalTestString.append(" shorted to ");
    electricalTestString.append(p->electricalTestPinName(loc2));
}

void MainWindow::programmerReadStatusChanged(ReadStatus newStatus)
{
    switch (newStatus)
    {
    case ReadStarting:
        if (readVerifying)
        {
            ui->statusLabel->setText("Verifying SIMM contents...");
        }
        else
        {
            ui->statusLabel->setText("Reading SIMM contents...");
        }
        break;
    case ReadComplete:
        if (readVerifying)
        {
            // Re-open the write file temporarily to compare it against the buffer.
            QFile temp(ui->chosenWriteFile->text());
            if (!temp.open(QFile::ReadOnly))
            {
                QMessageBox::warning(this, "Verify failed", "Unable to open file for verification.");
            }
            else
            {
                QByteArray fileBytes = temp.readAll();

                if (fileBytes.count() <= verifyArray->count())
                {
                    const char *fileBytesPtr = fileBytes.constData();
                    const char *readBytesPtr = verifyArray->constData();

                    if (memcmp(fileBytesPtr, readBytesPtr, fileBytes.count()) != 0)
                    {
                        QMessageBox::warning(this, "Verify error", "The data read back from the SIMM did not match the data written to it.");
                    }
                    else
                    {
                        QMessageBox::information(this, "Write complete", "The write operation finished, and the contents were verified successfully.");
                    }
                }
                else
                {
                    QMessageBox::warning(this, "Verify error", "The data read back from the SIMM did not match the data written to it.");
                }
            }

            // Close stuff not needed anymore
            if (verifyBuffer)
            {
                verifyBuffer->close();
                delete verifyBuffer;
                verifyBuffer = NULL;
            }

            if (verifyArray)
            {
                delete verifyArray;
                verifyArray = NULL;
            }

            ui->pages->setCurrentWidget(ui->controlPage);
        }
        else
        {
            if (readFile)
            {
                readFile->close();
                delete readFile;
                readFile = NULL;
            }

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::information(this, "Read complete", "The read operation finished.");
        }
        break;
    case ReadError:
        if (readVerifying)
        {
            if (verifyBuffer)
            {
                verifyBuffer->close();
                delete verifyBuffer;
            }
            verifyBuffer = NULL;
            if (verifyArray) delete verifyArray;
            verifyArray = NULL;

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::warning(this, "Verify error", "An error occurred reading the SIMM contents for verification.");
        }
        else
        {
            if (readFile)
            {
                readFile->close();
                delete readFile;
                readFile = NULL;
            }

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::warning(this, "Read error", "An error occurred reading from the SIMM.");
        }
        break;
    case ReadCancelled:
        if (readVerifying)
        {
            if (verifyBuffer)
            {
                verifyBuffer->close();
                delete verifyBuffer;
            }
            verifyBuffer = NULL;
            if (verifyArray) delete verifyArray;
            verifyArray = NULL;

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::warning(this, "Verify cancelled", "The verify operation was cancelled.");
        }
        else
        {
            if (readFile)
            {
                readFile->close();
                delete readFile;
                readFile = NULL;
            }

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::warning(this, "Read cancelled", "The read operation was cancelled.");
        }
        break;
    case ReadTimedOut:
        if (readVerifying)
        {
            if (verifyBuffer)
            {
                verifyBuffer->close();
                delete verifyBuffer;
            }
            verifyBuffer = NULL;
            if (verifyArray) delete verifyArray;
            verifyArray = NULL;

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::warning(this, "Verify timed out", "The verify operation timed out.");
        }
        else
        {
            if (readFile)
            {
                readFile->close();
                delete readFile;
                readFile = NULL;
            }

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::warning(this, "Read timed out", "The read operation timed out.");
        }
        break;
    }
}

void MainWindow::programmerReadTotalLengthChanged(uint32_t totalLen)
{
    ui->progressBar->setMaximum((int)totalLen);
}

void MainWindow::programmerReadCompletionLengthChanged(uint32_t len)
{
    ui->progressBar->setValue((int)len);
}

void MainWindow::programmerIdentifyStatusChanged(IdentificationStatus newStatus)
{
    switch (newStatus)
    {
    case IdentificationStarting:
        ui->statusLabel->setText("Identifying chips...");
        break;
    case IdentificationComplete:
    {
        ui->pages->setCurrentWidget(ui->controlPage);
        QString identifyString = "The chips identified themselves as:";
        for (int x = 0; x < 4; x++)
        {
            QString thisString;
            uint8_t manufacturer = 0;
            uint8_t device = 0;
            p->getChipIdentity(x, &manufacturer, &device);
            thisString.sprintf("\nIC%d: Manufacturer 0x%02X, Device 0x%02X", (x + 1), manufacturer, device);
            identifyString.append(thisString);
        }

        QMessageBox::information(this, "Identification complete", identifyString);
        break;
    }
    case IdentificationError:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Identification error", "An error occurred identifying the chips on the SIMM.");
        break;
    case IdentificationTimedOut:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Identification timed out", "The identification operation timed out.");
        break;
    }
}

void MainWindow::programmerFirmwareFlashStatusChanged(FirmwareFlashStatus newStatus)
{
    switch (newStatus)
    {
    case FirmwareFlashStarting:
        ui->statusLabel->setText("Flashing new firmware...");
        break;
    case FirmwareFlashComplete:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::information(this, "Firmware update complete", "The firmware update operation finished.");
        break;
    case FirmwareFlashError:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Firmware update error", "An error occurred writing firmware to the device.");
        break;
    case FirmwareFlashCancelled:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Firmware update cancelled", "The firmware update was cancelled.");
        break;
    case FirmwareFlashTimedOut:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Firmware update timed out", "The firmware update operation timed out.");
        break;
    }
}

void MainWindow::programmerFirmwareFlashTotalLengthChanged(uint32_t totalLen)
{
    ui->progressBar->setMaximum((int)totalLen);
}

void MainWindow::programmerFirmwareFlashCompletionLengthChanged(uint32_t len)
{
    ui->progressBar->setValue((int)len);
}


void MainWindow::on_actionUpdate_firmware_triggered()
{
    QString filename = QFileDialog::getOpenFileName(this, "Select a firmware image:");
    if (!filename.isNull())
    {
        resetAndShowStatusPage();
        p->flashFirmware(filename);
        qDebug() << "Updating firmware...";
    }
}

void MainWindow::on_identifyButton_clicked()
{
    resetAndShowStatusPage();
    p->identifySIMMChips();
}

void MainWindow::programmerBoardConnected()
{
    ui->pages->setCurrentWidget(ui->controlPage);
    ui->actionUpdate_firmware->setEnabled(true);
}

void MainWindow::programmerBoardDisconnected()
{
    ui->pages->setCurrentWidget(ui->notConnectedPage);
    ui->actionUpdate_firmware->setEnabled(false);
}

void MainWindow::programmerBoardDisconnectedDuringOperation()
{
    ui->pages->setCurrentWidget(ui->notConnectedPage);
    ui->actionUpdate_firmware->setEnabled(false);
    // Make sure any files have been closed if we were in the middle of something.
    if (writeFile)
    {
        writeFile->close();
        delete writeFile;
        writeFile = NULL;
    }
    if (readFile)
    {
        readFile->close();
        delete readFile;
        readFile = NULL;
    }
    QMessageBox::warning(this, "Programmer lost connection", "Lost contact with the programmer board. Unplug it, plug it back in, and try again.");
}

void MainWindow::resetAndShowStatusPage()
{
    //ui->progressBar->setValue(0);
    // Show indeterminate progress bar until communication succeeds/fails
    ui->progressBar->setRange(0, 0);
    ui->statusLabel->setText("Communicating with programmer (this may take a few seconds)...");
    ui->pages->setCurrentWidget(ui->statusPage);
}

void MainWindow::on_simmCapacityBox_currentIndexChanged(int index)
{
    uint32_t newCapacity = (uint32_t)ui->simmCapacityBox->itemData(index).toUInt();
    p->setSIMMCapacity(newCapacity);
}
