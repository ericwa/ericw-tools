#include "face.h"

#include <QTableWidget>
#include <QVBoxLayout>
#include <QHeaderView>

#include <QtGlobal> // for qDebug()
#include <QDebug> // for QDebug

#include <common/bspfile.hh>
#include <common/bsputils.hh>

FacePanel::FacePanel(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    QStringList labels;
    labels << QStringLiteral("key");
    labels << QStringLiteral("value");
    m_table->setHorizontalHeaderLabels(labels);

    // make the columns fill the table
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);

    // make the m_table completely fill `this`
    layout->addWidget(m_table, 1);
    layout->setContentsMargins(0, 0, 0, 0);
}

void FacePanel::addStat(const QString &str, const QString &value)
{
    // add a row
    int currentRow = m_table->rowCount();
    m_table->setRowCount(currentRow + 1);

    // populate it
    auto *labelItem = new QTableWidgetItem(str);
    labelItem->setFlags(labelItem->flags() & (~Qt::ItemIsEditable));
    m_table->setItem(currentRow, 0, labelItem);

    auto *valueItem = new QTableWidgetItem(value);
    valueItem->setFlags(valueItem->flags() & (~Qt::ItemIsEditable));
    m_table->setItem(currentRow, 1, valueItem);
}

void FacePanel::addStat(const QString &str, int value)
{
    QLocale locale(QLocale::English, QLocale::UnitedStates);
    addStat(str, locale.toString(value));
}

void FacePanel::updateWithBSP(const mbsp_t *bsp, const std::vector<entdict_t> &ents, const bspxentries_t &entries, const qvec3f &cameraPos, const qvec3f &cameraForward)
{
    if (bsp == nullptr) {
        m_lastFace = nullptr;
        return;
    }

    float bestLen = std::numeric_limits<float>::infinity();
    const mface_t *bestFace = nullptr;
    const dmodelh2_t *bestModel = nullptr;

    for (size_t m = 0; m < bsp->dmodels.size(); m++) {
        qvec3f offset {};
        /* Find the entity for the model */
        std::string modelname = fmt::format("*{}", m);
        const entdict_t *entdict = EntData_Find(ents, "model", modelname);

        if (entdict != nullptr) {
            /* Set up the offset for rotate_* entities */
            entdict->get_vector("origin", offset);
        }

        for (size_t i = bsp->dmodels[m].firstface; i < bsp->dmodels[m].firstface + bsp->dmodels[m].numfaces; i++) {

            qvec3f centroid = Face_Centroid(bsp, &bsp->dfaces[i]) + offset;

            qvec3f sub = cameraPos - centroid;
            auto plane = Face_Plane(bsp, &bsp->dfaces[i]);

            if (qv::dot(sub, plane.normal) < 0) {
                continue;
            }

            float len = qv::normalizeInPlace(sub);
            if (len < bestLen) {
                bestFace = &bsp->dfaces[i];
                bestLen = len;
                bestModel = &bsp->dmodels[m];
            }
        }
    }

    if (!bestFace || bestFace == m_lastFace) {
        return;
    }
    
    m_table->setRowCount(0);
    m_lastFace = bestFace;
    
    addStat(QStringLiteral("model id"), bestModel - bsp->dmodels.data());
    addStat(QStringLiteral("face id"), bestFace - bsp->dfaces.data());
    addStat(QStringLiteral("plane id"), bestFace->planenum);
    addStat(QStringLiteral("texinfo id"), bestFace->texinfo);
    addStat(QStringLiteral("plane"), QString::fromStdString(fmt::format("{} {}", bsp->dplanes[bestFace->planenum].normal, bsp->dplanes[bestFace->planenum].dist)));
    addStat(QStringLiteral("texture"), bsp->texinfo[bestFace->texinfo].texture.data());
    addStat(QStringLiteral("lightofs"), bestFace->lightofs);
    addStat(QStringLiteral("flags"), QString::fromStdString(fmt::format("{}", bsp->texinfo[bestFace->texinfo].flags.native)));
    addStat(QStringLiteral("translucence"), QString::fromStdString(fmt::format("{}", bsp->texinfo[bestFace->texinfo].translucence)));
}
