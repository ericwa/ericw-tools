#pragma once

#include <QWidget>
#include <common/bspfile.hh>
#include <common/entdata.h>

struct mbsp_t;
class QTableWidget;

class FacePanel : public QWidget
{
private:
    QTableWidget *m_table;
    const mface_t *m_lastFace = nullptr;

public:
    FacePanel(QWidget *parent = nullptr);

private:
    void addStat(const QString &str, const QString &value);
    void addStat(const QString &str, int value);

public:
    void updateWithBSP(const mbsp_t *bsp, const std::vector<entdict_t> &ents, const bspxentries_t &entries, int face_id);
};
