#include "stats.h"

#include <QTableWidget>
#include <QVBoxLayout>
#include <QHeaderView>

#include <QtGlobal> // for qDebug()
#include <QDebug> // for QDebug

#include <common/bspfile.hh>

StatsPanel::StatsPanel(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    QStringList labels;
    labels << QStringLiteral("stat");
    labels << QStringLiteral("count");
    m_table->setHorizontalHeaderLabels(labels);

    // make the columns fill the table
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);

    // make the m_table completely fill `this`
    layout->addWidget(m_table, 1);
    layout->setContentsMargins(0, 0, 0, 0);
}

void StatsPanel::addStat(const QString &str, int value)
{
    // add a row
    int currentRow = m_table->rowCount();
    m_table->setRowCount(currentRow + 1);

    // populate it
    auto *labelItem = new QTableWidgetItem(str);
    labelItem->setFlags(labelItem->flags() & (~Qt::ItemIsEditable));
    m_table->setItem(currentRow, 0, labelItem);

    QLocale locale(QLocale::English, QLocale::UnitedStates);

    auto *valueItem = new QTableWidgetItem(locale.toString(value));
    valueItem->setFlags(valueItem->flags() & (~Qt::ItemIsEditable));
    m_table->setItem(currentRow, 1, valueItem);
}

void StatsPanel::updateWithBSP(const mbsp_t *bsp, const bspxentries_t &entries)
{
    m_table->setRowCount(0);

    if (bsp == nullptr) {
        return;
    }

    addStat(QStringLiteral("models"), bsp->dmodels.size());
    addStat(QStringLiteral("nodes"), bsp->dnodes.size());
    addStat(QStringLiteral("leafs"), bsp->dleafs.size());
    addStat(QStringLiteral("clipnodes"), bsp->dclipnodes.size());
    addStat(QStringLiteral("planes"), bsp->dplanes.size());
    addStat(QStringLiteral("vertexes"), bsp->dvertexes.size());
    addStat(QStringLiteral("faces"), bsp->dfaces.size());
    addStat(QStringLiteral("surfedges"), bsp->dsurfedges.size());
    addStat(QStringLiteral("edges"), bsp->dedges.size());
    addStat(QStringLiteral("leaffaces"), bsp->dleaffaces.size());
    addStat(QStringLiteral("leafbrushes"), bsp->dleafbrushes.size());

    addStat(QStringLiteral("areas"), bsp->dareas.size());
    addStat(QStringLiteral("areaportals"), bsp->dareaportals.size());

    addStat(QStringLiteral("brushes"), bsp->dbrushes.size());
    addStat(QStringLiteral("brushsides"), bsp->dbrushsides.size());

    addStat(QStringLiteral("texinfos"), bsp->texinfo.size());
    addStat(QStringLiteral("textures"), bsp->dtex.textures.size());

    addStat(QStringLiteral("visdata bytes"), bsp->dvis.bits.size());
    addStat(QStringLiteral("lightdata bytes"), bsp->dlightdata.size());
    addStat(QStringLiteral("entdata bytes"), bsp->dentdata.size());

    // bspx lumps
    for (const auto &[lumpname, data] : entries) {
        addStat(QStringLiteral("%1 bytes").arg(lumpname.c_str()), data.size());
    }
}
