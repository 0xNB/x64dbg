#include <QListWidget>
#include "MiscUtil.h"
#include "CPUWidget.h"
#include "CPUDisassembly.h"
#include "CPUMultiDump.h"
#include "Configuration.h"
#include "WordEditDialog.h"
#include "LineEditDialog.h"
#include "EditFloatRegister.h"
#include "SelectFields.h"
#include "CPURegistersView.h"
#include "ldconvert.h"

CPURegistersView::CPURegistersView(CPUWidget* parent) : RegistersView(parent), mParent(parent)
{
    // precreate ContextMenu Actions
    wCM_Modify = new QAction(DIcon("register_edit"), tr("Modify value"), this);
    wCM_Modify->setShortcut(QKeySequence(Qt::Key_Enter));
    wCM_ToggleValue = setupAction(DIcon("register_toggle"), tr("Toggle"));
    wCM_Undo = setupAction(DIcon("undo"), tr("Undo"));
    wCM_CopyPrevious = setupAction(DIcon("undo"), "");
    wCM_FollowInDisassembly = new QAction(DIcon(QString("processor%1").arg(ArchValue("32", "64"))), tr("Follow in Disassembler"), this);
    wCM_FollowInDump = new QAction(DIcon("dump"), tr("Follow in Dump"), this);
    wCM_FollowInStack = new QAction(DIcon("stack"), tr("Follow in Stack"), this);
    wCM_FollowInMemoryMap = new QAction(DIcon("memmap_find_address_page"), tr("Follow in Memory Map"), this);
    wCM_RemoveHardware = new QAction(DIcon("breakpoint_remove"), tr("&Remove hardware breakpoint"), this);
    wCM_Incrementx87Stack = setupAction(DIcon("arrow-small-down"), tr("Increment x87 Stack"));
    wCM_Decrementx87Stack = setupAction(DIcon("arrow-small-up"), tr("Decrement x87 Stack"));
    wCM_Highlight = setupAction(DIcon("highlight"), tr("Highlight"));
    // foreign messages
    connect(Bridge::getBridge(), SIGNAL(updateRegisters()), this, SLOT(updateRegistersSlot()));
    connect(this, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(displayCustomContextMenuSlot(QPoint)));
    connect(Bridge::getBridge(), SIGNAL(dbgStateChanged(DBGSTATE)), this, SLOT(debugStateChangedSlot(DBGSTATE)));
    connect(parent->getDisasmWidget(), SIGNAL(selectionChanged(dsint)), this, SLOT(disasmSelectionChangedSlot(dsint)));
    // context menu actions
    connect(wCM_Incrementx87Stack, SIGNAL(triggered()), this, SLOT(onIncrementx87StackAction()));
    connect(wCM_Decrementx87Stack, SIGNAL(triggered()), this, SLOT(onDecrementx87StackAction()));
    connect(wCM_Modify, SIGNAL(triggered()), this, SLOT(onModifyAction()));
    connect(wCM_ToggleValue, SIGNAL(triggered()), this, SLOT(onToggleValueAction()));
    connect(wCM_Undo, SIGNAL(triggered()), this, SLOT(onUndoAction()));
    connect(wCM_CopyPrevious, SIGNAL(triggered()), this, SLOT(onCopyPreviousAction()));
    connect(wCM_FollowInDisassembly, SIGNAL(triggered()), this, SLOT(onFollowInDisassembly()));
    connect(wCM_FollowInDump, SIGNAL(triggered()), this, SLOT(onFollowInDump()));
    connect(wCM_FollowInStack, SIGNAL(triggered()), this, SLOT(onFollowInStack()));
    connect(wCM_FollowInMemoryMap, SIGNAL(triggered()), this, SLOT(onFollowInMemoryMap()));
    connect(wCM_RemoveHardware, SIGNAL(triggered()), this, SLOT(onRemoveHardware()));
    connect(wCM_Highlight, SIGNAL(triggered()), this, SLOT(onHighlightSlot()));

    refreshShortcutsSlot();
    connect(Config(), SIGNAL(shortcutsUpdated()), this, SLOT(refreshShortcutsSlot()));
}

void CPURegistersView::refreshShortcutsSlot()
{
    wCM_ToggleValue->setShortcut(ConfigShortcut("ActionToggleRegisterValue"));
    wCM_Highlight->setShortcut(ConfigShortcut("ActionHighlightingMode"));
    wCM_Incrementx87Stack->setShortcut(ConfigShortcut("ActionIncrementx87Stack"));
    wCM_Decrementx87Stack->setShortcut(ConfigShortcut("ActionDecrementx87Stack"));
    RegistersView::refreshShortcutsSlot();
}

void CPURegistersView::mousePressEvent(QMouseEvent* event)
{
    if(!isActive)
        return;

    if(event->y() < yTopSpacing - mButtonHeight)
    {
        onChangeFPUViewAction();
    }
    else
    {
        // get mouse position
        const int y = (event->y() - yTopSpacing) / (double)mRowHeight;
        const int x = event->x() / (double)mCharWidth;

        REGISTER_NAME r;
        // do we find a corresponding register?
        if(identifyRegister(y, x, &r))
        {
            Disassembly* CPUDisassemblyView = mParent->getDisasmWidget();
            if(CPUDisassemblyView->isHighlightMode())
            {
                if(mGPR.contains(r) && r != REGISTER_NAME::EFLAGS)
                    CPUDisassemblyView->hightlightToken(ZydisTokenizer::SingleToken(ZydisTokenizer::TokenType::GeneralRegister, mRegisterMapping.constFind(r).value()));
                else if(mFPUMMX.contains(r))
                    CPUDisassemblyView->hightlightToken(ZydisTokenizer::SingleToken(ZydisTokenizer::TokenType::MmxRegister, mRegisterMapping.constFind(r).value()));
                else if(mFPUXMM.contains(r))
                    CPUDisassemblyView->hightlightToken(ZydisTokenizer::SingleToken(ZydisTokenizer::TokenType::XmmRegister, mRegisterMapping.constFind(r).value()));
                else if(mFPUYMM.contains(r))
                    CPUDisassemblyView->hightlightToken(ZydisTokenizer::SingleToken(ZydisTokenizer::TokenType::YmmRegister, mRegisterMapping.constFind(r).value()));
                else if(mSEGMENTREGISTER.contains(r))
                    CPUDisassemblyView->hightlightToken(ZydisTokenizer::SingleToken(ZydisTokenizer::TokenType::MemorySegment, mRegisterMapping.constFind(r).value()));
                else
                    mSelected = r;
            }
            else
                mSelected = r;
            emit refresh();
        }
        else
            mSelected = UNKNOWN;
    }
}

void CPURegistersView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if(!isActive || event->button() != Qt::LeftButton)
        return;
    // get mouse position
    const int y = (event->y() - yTopSpacing) / (double)mRowHeight;
    const int x = event->x() / (double)mCharWidth;

    // do we find a corresponding register?
    if(!identifyRegister(y, x, 0))
        return;
    if(mSelected == CIP) //double clicked on CIP register, disasm CIP
        DbgCmdExec("disasm cip");
    // is current register general purposes register or FPU register?
    else if(mMODIFYDISPLAY.contains(mSelected))
        wCM_Modify->trigger();
    else if(mBOOLDISPLAY.contains(mSelected)) // is flag ?
        wCM_ToggleValue->trigger(); //toggle flag value
    else if(mCANSTOREADDRESS.contains(mSelected))
        wCM_FollowInDisassembly->trigger(); //follow in disassembly
    else if(mSelected == ArchValue(FS, GS)) // double click on FS or GS, follow TEB in dump
        DbgCmdExec("dump teb()");
}

void CPURegistersView::keyPressEvent(QKeyEvent* event)
{
    if(isActive && (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return))
        wCM_Modify->trigger();
    else
        RegistersView::keyPressEvent(event);
}

void CPURegistersView::debugStateChangedSlot(DBGSTATE state)
{
    if(state == stopped)
    {
        updateRegistersSlot();
        isActive = false;
    }
    else
    {
        isActive = true;
    }
}

void CPURegistersView::updateRegistersSlot()
{
    // read registers
    REGDUMP z;
    DbgGetRegDumpEx(&z, sizeof(REGDUMP));
    // update gui
    setRegisters(&z);
}

void CPURegistersView::ModifyFields(const QString & title, STRING_VALUE_TABLE_t* table, SIZE_T size)
{
    SelectFields mSelectFields(this);
    QListWidget* mQListWidget = mSelectFields.GetList();

    QStringList items;
    unsigned int i;

    for(i = 0; i < size; i++)
        items << QApplication::translate("RegistersView_ConstantsOfRegisters", table[i].string) + QString(" (%1)").arg(table[i].value, 0, 16);

    mQListWidget->addItems(items);

    mSelectFields.setWindowTitle(title);
    if(mSelectFields.exec() != QDialog::Accepted)
        return;

    if(mQListWidget->selectedItems().count() != 1)
        return;

    //QListWidgetItem* item = mQListWidget->takeItem(mQListWidget->currentRow());
    QString itemText = mQListWidget->item(mQListWidget->currentRow())->text();

    duint value;

    for(i = 0; i < size; i++)
    {
        if(QApplication::translate("RegistersView_ConstantsOfRegisters", table[i].string) + QString(" (%1)").arg(table[i].value, 0, 16) == itemText)
            break;
    }

    value = table[i].value;

    setRegister(mSelected, (duint)value);
    //delete item;
}

extern STRING_VALUE_TABLE_t MxCsrRCValueStringTable[4];
extern STRING_VALUE_TABLE_t ControlWordRCValueStringTable[4];
extern STRING_VALUE_TABLE_t StatusWordTOPValueStringTable[8];
extern STRING_VALUE_TABLE_t ControlWordPCValueStringTable[4];
extern STRING_VALUE_TABLE_t TagWordValueStringTable[4];

#define MODIFY_FIELDS_DISPLAY(prefix, title, table) ModifyFields(prefix + QChar(' ') + QString(title), (STRING_VALUE_TABLE_t *) & table, SIZE_TABLE(table) )

static void editSIMDRegister(CPURegistersView* parent, int bits, const QString & title, char* data, RegistersView::REGISTER_NAME mSelected)
{
    EditFloatRegister mEditFloat(bits, parent);
    mEditFloat.setWindowTitle(title);
    mEditFloat.loadData(data);
    mEditFloat.show();
    mEditFloat.selectAllText();
    if(mEditFloat.exec() == QDialog::Accepted)
        parent->setRegister(mSelected, (duint)mEditFloat.getData());
}

/**
 * @brief   This function displays the appropriate edit dialog according to selected register
 * @return  Nothing.
 */

void CPURegistersView::displayEditDialog()
{
    if(mFPU.contains(mSelected))
    {
        if(mTAGWORD.contains(mSelected))
            MODIFY_FIELDS_DISPLAY(tr("Edit"), "Tag " + mRegisterMapping.constFind(mSelected).value(), TagWordValueStringTable);
        else if(mSelected == MxCsr_RC)
            MODIFY_FIELDS_DISPLAY(tr("Edit"), "MxCsr_RC", MxCsrRCValueStringTable);
        else if(mSelected == x87CW_RC)
            MODIFY_FIELDS_DISPLAY(tr("Edit"), "x87CW_RC", ControlWordRCValueStringTable);
        else if(mSelected == x87CW_PC)
            MODIFY_FIELDS_DISPLAY(tr("Edit"), "x87CW_PC", ControlWordPCValueStringTable);
        else if(mSelected == x87SW_TOP)
        {
            MODIFY_FIELDS_DISPLAY(tr("Edit"), "x87SW_TOP", StatusWordTOPValueStringTable);
            // if(mFpuMode == false)
            updateRegistersSlot();
        }
        else if(mFPUYMM.contains(mSelected))
            editSIMDRegister(this, 256, tr("Edit YMM register"), registerValue(&wRegDumpStruct, mSelected), mSelected);
        else if(mFPUXMM.contains(mSelected))
            editSIMDRegister(this, 128, tr("Edit XMM register"), registerValue(&wRegDumpStruct, mSelected), mSelected);
        else if(mFPUMMX.contains(mSelected))
            editSIMDRegister(this, 64, tr("Edit MMX register"), registerValue(&wRegDumpStruct, mSelected), mSelected);
        else
        {
            bool errorinput = false;
            LineEditDialog mLineEdit(this);

            mLineEdit.setText(GetRegStringValueFromValue(mSelected,  registerValue(&wRegDumpStruct, mSelected)));
            mLineEdit.setWindowTitle(tr("Edit FPU register"));
            mLineEdit.setWindowIcon(DIcon("log"));
            mLineEdit.setCursorPosition(0);
            auto sizeRegister = int(GetSizeRegister(mSelected));
            if(sizeRegister == 10)
                mLineEdit.setFpuMode();
            mLineEdit.ForceSize(sizeRegister * 2);
            do
            {
                errorinput = false;
                mLineEdit.show();
                mLineEdit.selectAllText();
                if(mLineEdit.exec() != QDialog::Accepted)
                    return; //pressed cancel
                else
                {
                    bool ok = false;
                    duint fpuvalue;

                    if(mUSHORTDISPLAY.contains(mSelected))
                        fpuvalue = (duint) mLineEdit.editText.toUShort(&ok, 16);
                    else if(mDWORDDISPLAY.contains(mSelected))
                        fpuvalue = mLineEdit.editText.toUInt(&ok, 16);
                    else if(mFPUx87_80BITSDISPLAY.contains(mSelected))
                    {
                        QString editTextLower = mLineEdit.editText.toLower();
                        if(sizeRegister == 10 && (mLineEdit.editText.contains(QChar('.')) || editTextLower == "nan" || editTextLower == "inf"
                                                  || editTextLower == "+inf" || editTextLower == "-inf"))
                        {
                            char number[10];
                            str2ld(mLineEdit.editText.toUtf8().constData(), number);
                            setRegister(mSelected, reinterpret_cast<duint>(number));
                            return;
                        }
                        else
                        {
                            QByteArray pArray =  mLineEdit.editText.toLocal8Bit();

                            if(pArray.size() == sizeRegister * 2)
                            {
                                char* pData = (char*) calloc(1, sizeof(char) * sizeRegister);

                                if(pData != NULL)
                                {
                                    ok = true;
                                    char actual_char[3];
                                    for(int i = 0; i < sizeRegister; i++)
                                    {
                                        memset(actual_char, 0, sizeof(actual_char));
                                        memcpy(actual_char, (char*) pArray.data() + (i * 2), 2);
                                        if(! isxdigit(actual_char[0]) || ! isxdigit(actual_char[1]))
                                        {
                                            ok = false;
                                            break;
                                        }
                                        pData[i] = (char)strtol(actual_char, NULL, 16);
                                    }

                                    if(ok)
                                    {
                                        if(!ConfigBool("Gui", "FpuRegistersLittleEndian")) // reverse byte order if it is big-endian
                                        {
                                            pArray = ByteReverse(QByteArray(pData, sizeRegister));
                                            setRegister(mSelected, reinterpret_cast<duint>(pArray.constData()));
                                        }
                                        else
                                            setRegister(mSelected, reinterpret_cast<duint>(pData));
                                    }

                                    free(pData);

                                    if(ok)
                                        return;
                                }
                            }
                        }
                    }
                    if(!ok)
                    {
                        errorinput = true;

                        SimpleWarningBox(this, tr("ERROR CONVERTING TO HEX"), tr("ERROR CONVERTING TO HEX"));
                    }
                    else
                        setRegister(mSelected, fpuvalue);
                }
            }
            while(errorinput);
        }
    }
    else if(mSelected == LastError)
    {
        bool errorinput = false;
        LineEditDialog mLineEdit(this);
        LASTERROR* error = (LASTERROR*)registerValue(&wRegDumpStruct, LastError);
        mLineEdit.setText(QString::number(error->code, 16));
        mLineEdit.setWindowTitle(tr("Set Last Error"));
        mLineEdit.setCursorPosition(0);
        do
        {
            errorinput = true;
            mLineEdit.show();
            mLineEdit.selectAllText();
            if(mLineEdit.exec() != QDialog::Accepted)
                return;
            if(DbgIsValidExpression(mLineEdit.editText.toUtf8().constData()))
                errorinput = false;
        }
        while(errorinput);
        setRegister(LastError, DbgValFromString(mLineEdit.editText.toUtf8().constData()));
    }
    else if(mSelected == LastStatus)
    {
        bool statusinput = false;
        LineEditDialog mLineEdit(this);
        LASTSTATUS* status = (LASTSTATUS*)registerValue(&wRegDumpStruct, LastStatus);
        mLineEdit.setText(QString::number(status->code, 16));
        mLineEdit.setWindowTitle(tr("Set Last Status"));
        mLineEdit.setCursorPosition(0);
        do
        {
            statusinput = true;
            mLineEdit.show();
            mLineEdit.selectAllText();
            if(mLineEdit.exec() != QDialog::Accepted)
                return;
            if(DbgIsValidExpression(mLineEdit.editText.toUtf8().constData()))
                statusinput = false;
        }
        while(statusinput);
        setRegister(LastStatus, DbgValFromString(mLineEdit.editText.toUtf8().constData()));
    }
    else
    {
        WordEditDialog wEditDial(this);
        wEditDial.setup(tr("Edit"), (* ((duint*) registerValue(&wRegDumpStruct, mSelected))), sizeof(dsint));
        if(wEditDial.exec() == QDialog::Accepted) //OK button clicked
            setRegister(mSelected, wEditDial.getVal());
    }
}

void CPURegistersView::CreateDumpNMenu(QMenu* dumpMenu)
{
    QList<QString> names;
    CPUMultiDump* multiDump = mParent->getDumpWidget();
    dumpMenu->setIcon(DIcon("dump"));
    int maxDumps = multiDump->getMaxCPUTabs();
    multiDump->getTabNames(names);
    for(int i = 0; i < maxDumps; i++)
    {
        QAction* action = new QAction(names.at(i), this);
        connect(action, SIGNAL(triggered()), this, SLOT(onFollowInDumpN()));
        dumpMenu->addAction(action);
        action->setData(i + 1);
    }
}

void CPURegistersView::onIncrementx87StackAction()
{
    if(mFPUx87_80BITSDISPLAY.contains(mSelected))
        setRegister(x87SW_TOP, ((* ((duint*) registerValue(&wRegDumpStruct, x87SW_TOP))) + 1) % 8);
}

void CPURegistersView::onDecrementx87StackAction()
{
    if(mFPUx87_80BITSDISPLAY.contains(mSelected))
        setRegister(x87SW_TOP, ((* ((duint*) registerValue(&wRegDumpStruct, x87SW_TOP))) - 1) % 8);
}

void CPURegistersView::onModifyAction()
{
    if(mMODIFYDISPLAY.contains(mSelected))
        displayEditDialog();
}

void CPURegistersView::onToggleValueAction()
{
    if(mBOOLDISPLAY.contains(mSelected))
    {
        int value = (int)(* (bool*) registerValue(&wRegDumpStruct, mSelected));
        setRegister(mSelected, value ^ 1);
    }
}

void CPURegistersView::onUndoAction()
{
    if(mUNDODISPLAY.contains(mSelected))
    {
        if(mFPUMMX.contains(mSelected) || mFPUXMM.contains(mSelected) || mFPUYMM.contains(mSelected) || mFPUx87_80BITSDISPLAY.contains(mSelected))
            setRegister(mSelected, (duint)registerValue(&wCipRegDumpStruct, mSelected));
        else
            setRegister(mSelected, *(duint*)registerValue(&wCipRegDumpStruct, mSelected));
    }
}

void CPURegistersView::onCopyPreviousAction()
{
    Bridge::CopyToClipboard(wCM_CopyPrevious->data().toString());
}

void CPURegistersView::onHighlightSlot()
{
    Disassembly* CPUDisassemblyView = mParent->getDisasmWidget();
    if(mGPR.contains(mSelected) && mSelected != REGISTER_NAME::EFLAGS)
        CPUDisassemblyView->hightlightToken(ZydisTokenizer::SingleToken(ZydisTokenizer::TokenType::GeneralRegister, mRegisterMapping.constFind(mSelected).value()));
    else if(mSEGMENTREGISTER.contains(mSelected))
        CPUDisassemblyView->hightlightToken(ZydisTokenizer::SingleToken(ZydisTokenizer::TokenType::MemorySegment, mRegisterMapping.constFind(mSelected).value()));
    else if(mFPUMMX.contains(mSelected))
        CPUDisassemblyView->hightlightToken(ZydisTokenizer::SingleToken(ZydisTokenizer::TokenType::MmxRegister, mRegisterMapping.constFind(mSelected).value()));
    else if(mFPUXMM.contains(mSelected))
        CPUDisassemblyView->hightlightToken(ZydisTokenizer::SingleToken(ZydisTokenizer::TokenType::XmmRegister, mRegisterMapping.constFind(mSelected).value()));
    else if(mFPUYMM.contains(mSelected))
        CPUDisassemblyView->hightlightToken(ZydisTokenizer::SingleToken(ZydisTokenizer::TokenType::YmmRegister, mRegisterMapping.constFind(mSelected).value()));
    CPUDisassemblyView->reloadData();
}

void CPURegistersView::onFollowInDisassembly()
{
    if(mCANSTOREADDRESS.contains(mSelected))
    {
        QString addr = QString("%1").arg((* ((duint*) registerValue(&wRegDumpStruct, mSelected))), mRegisterPlaces[mSelected].valuesize, 16, QChar('0')).toUpper();
        if(DbgMemIsValidReadPtr((* ((duint*) registerValue(&wRegDumpStruct, mSelected)))))
            DbgCmdExec(QString().sprintf("disasm \"%s\"", addr.toUtf8().constData()));
    }
}

void CPURegistersView::onFollowInDump()
{
    if(mCANSTOREADDRESS.contains(mSelected))
    {
        QString addr = QString("%1").arg((* ((duint*) registerValue(&wRegDumpStruct, mSelected))), mRegisterPlaces[mSelected].valuesize, 16, QChar('0')).toUpper();
        if(DbgMemIsValidReadPtr((* ((duint*) registerValue(&wRegDumpStruct, mSelected)))))
            DbgCmdExec(QString().sprintf("dump \"%s\"", addr.toUtf8().constData()));
    }
}

void CPURegistersView::onFollowInDumpN()
{
    if(mCANSTOREADDRESS.contains(mSelected))
    {
        QString addr = QString("%1").arg((* ((duint*) registerValue(&wRegDumpStruct, mSelected))), mRegisterPlaces[mSelected].valuesize, 16, QChar('0')).toUpper();
        if(DbgMemIsValidReadPtr((* ((duint*) registerValue(&wRegDumpStruct, mSelected)))))
        {
            QAction* action = qobject_cast<QAction*>(sender());
            int numDump = action->data().toInt();
            DbgCmdExec(QString("dump %1, .%2").arg(addr).arg(numDump));
        }
    }
}

void CPURegistersView::onFollowInStack()
{
    if(mCANSTOREADDRESS.contains(mSelected))
    {
        QString addr = QString("%1").arg((* ((duint*) registerValue(&wRegDumpStruct, mSelected))), mRegisterPlaces[mSelected].valuesize, 16, QChar('0')).toUpper();
        if(DbgMemIsValidReadPtr((* ((duint*) registerValue(&wRegDumpStruct, mSelected)))))
            DbgCmdExec(QString().sprintf("sdump \"%s\"", addr.toUtf8().constData()));
    }
}

void CPURegistersView::onFollowInMemoryMap()
{
    if(mCANSTOREADDRESS.contains(mSelected))
    {
        QString addr = QString("%1").arg((* ((duint*) registerValue(&wRegDumpStruct, mSelected))), mRegisterPlaces[mSelected].valuesize, 16, QChar('0')).toUpper();
        if(DbgMemIsValidReadPtr((* ((duint*) registerValue(&wRegDumpStruct, mSelected)))))
            DbgCmdExec(QString().sprintf("memmapdump \"%s\"", addr.toUtf8().constData()));
    }
}

void CPURegistersView::onRemoveHardware()
{
    if(mSelected == DR0 || mSelected == DR1 || mSelected == DR2 || mSelected == DR3)
    {
        QString addr = QString("%1").arg((* ((duint*) registerValue(&wRegDumpStruct, mSelected))), mRegisterPlaces[mSelected].valuesize, 16, QChar('0')).toUpper();
        DbgCmdExec(QString().sprintf("bphc \"%s\"", addr.toUtf8().constData()));
    }
}

void CPURegistersView::displayCustomContextMenuSlot(QPoint pos)
{
    if(!isActive)
        return;
    QMenu wMenu(this);
    QMenu* followInDumpNMenu = nullptr;
    setupSIMDModeMenu();

    if(mSelected != UNKNOWN)
    {
        if(mMODIFYDISPLAY.contains(mSelected))
        {
            wMenu.addAction(wCM_Modify);
        }

        if(mCANSTOREADDRESS.contains(mSelected))
        {
            duint addr = (* ((duint*) registerValue(&wRegDumpStruct, mSelected)));
            if(DbgMemIsValidReadPtr(addr))
            {
                wMenu.addAction(wCM_FollowInDump);
                followInDumpNMenu = new QMenu(tr("Follow in &Dump"), &wMenu);
                CreateDumpNMenu(followInDumpNMenu);
                wMenu.addMenu(followInDumpNMenu);
                wMenu.addAction(wCM_FollowInDisassembly);
                wMenu.addAction(wCM_FollowInMemoryMap);
                duint size = 0;
                duint base = DbgMemFindBaseAddr(DbgValFromString("csp"), &size);
                if(addr >= base && addr < base + size)
                    wMenu.addAction(wCM_FollowInStack);
            }
        }

        if(mSelected == DR0 || mSelected == DR1 || mSelected == DR2 || mSelected == DR3)
        {
            if(* ((duint*) registerValue(&wRegDumpStruct, mSelected)) != 0)
                wMenu.addAction(wCM_RemoveHardware);
        }

        wMenu.addAction(wCM_CopyToClipboard);
        if(mFPUx87_80BITSDISPLAY.contains(mSelected))
        {
            wMenu.addAction(wCM_CopyFloatingPointValueToClipboard);
        }
        if(mLABELDISPLAY.contains(mSelected))
        {
            QString symbol = getRegisterLabel(mSelected);
            if(symbol != "")
                wMenu.addAction(wCM_CopySymbolToClipboard);
        }
        wMenu.addAction(wCM_CopyAll);

        if((mGPR.contains(mSelected) && mSelected != REGISTER_NAME::EFLAGS) || mSEGMENTREGISTER.contains(mSelected) || mFPUMMX.contains(mSelected) || mFPUXMM.contains(mSelected) || mFPUYMM.contains(mSelected))
        {
            wMenu.addAction(wCM_Highlight);
        }

        if(mUNDODISPLAY.contains(mSelected) && CompareRegisters(mSelected, &wRegDumpStruct, &wCipRegDumpStruct) != 0)
        {
            wMenu.addAction(wCM_Undo);
            wCM_CopyPrevious->setData(GetRegStringValueFromValue(mSelected, registerValue(&wCipRegDumpStruct, mSelected)));
            wCM_CopyPrevious->setText(tr("Copy old value: %1").arg(wCM_CopyPrevious->data().toString()));
            wMenu.addAction(wCM_CopyPrevious);
        }

        if(mBOOLDISPLAY.contains(mSelected))
        {
            wMenu.addAction(wCM_ToggleValue);
        }

        if(mFPUx87_80BITSDISPLAY.contains(mSelected))
        {
            wMenu.addAction(wCM_Incrementx87Stack);
            wMenu.addAction(wCM_Decrementx87Stack);
        }

        if(mFPUMMX.contains(mSelected) || mFPUXMM.contains(mSelected) || mFPUYMM.contains(mSelected))
        {
            wMenu.addMenu(mSwitchSIMDDispMode);
        }

        if(mFPUMMX.contains(mSelected) || mFPUx87_80BITSDISPLAY.contains(mSelected))
        {
            if(mFpuMode != 0)
                wMenu.addAction(mDisplaySTX);
            if(mFpuMode != 1)
                wMenu.addAction(mDisplayx87rX);
            if(mFpuMode != 2)
                wMenu.addAction(mDisplayMMX);
        }

        wMenu.exec(this->mapToGlobal(pos));
    }
    else
    {
        wMenu.addSeparator();
        wMenu.addAction(wCM_ChangeFPUView);
        wMenu.addAction(wCM_CopyAll);
        wMenu.addMenu(mSwitchSIMDDispMode);
        if(mFpuMode != 0)
            wMenu.addAction(mDisplaySTX);
        if(mFpuMode != 1)
            wMenu.addAction(mDisplayx87rX);
        if(mFpuMode != 2)
            wMenu.addAction(mDisplayMMX);
        wMenu.addSeparator();
        QAction* wHwbpCsp = wMenu.addAction(DIcon("breakpoint"), tr("Set Hardware Breakpoint on %1").arg(ArchValue("ESP", "RSP")));
        QAction* wAction = wMenu.exec(this->mapToGlobal(pos));

        if(wAction == wHwbpCsp)
            DbgCmdExec("bphws csp,rw");
    }
}

void CPURegistersView::setRegister(REGISTER_NAME reg, duint value)
{
    // is register-id known?
    if(mRegisterMapping.contains(reg))
    {
        // map x87st0 to x87r0
        QString wRegName;
        if(reg >= x87st0 && reg <= x87st7)
            wRegName = QString().sprintf("st%d", reg - x87st0);
        else
            // map "cax" to "eax" or "rax"
            wRegName = mRegisterMapping.constFind(reg).value();

        // flags need to '_' infront
        if(mFlags.contains(reg))
            wRegName = "_" + wRegName;

        // we change the value (so highlight it)
        mRegisterUpdates.insert(reg);
        // tell everything the compiler
        if(mFPU.contains(reg))
            wRegName = "_" + wRegName;

        DbgValToString(wRegName.toUtf8().constData(), value);

        // force repaint
        emit refresh();
    }
}

void CPURegistersView::disasmSelectionChangedSlot(dsint va)
{
    mHighlightRegs = mParent->getDisasmWidget()->DisassembleAt(va - mParent->getDisasmWidget()->getBase()).regsReferenced;
    emit refresh();
}
