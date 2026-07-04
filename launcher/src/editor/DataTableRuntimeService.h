#pragma once

#include "BattleRuntime.h"
#include "models/BPValue.h"
#include "models/DataTable.h"
#include "models/AssetRef.h"

#include <QString>
#include <functional>

class DataTableRuntimeService {
public:
    using DiagnosticCallback = std::function<void(const QString&)>;

    explicit DataTableRuntimeService(QString projectRoot = {});

    void setProjectRoot(const QString& projectRoot) { m_projectRoot = projectRoot; }
    void setDiagnosticCallback(DiagnosticCallback callback) { m_diagnosticCallback = std::move(callback); }
    QString resolvePath(const QString& tableRef) const;
    bool load(const QString& tableRef, DataTable* outTable) const;
    BPValue fieldValue(const QString& tableRef, const QString& recordId, const QString& fieldName) const;
    int recordCount(const QString& tableRef) const;
    QList<BattleCard> cardsFromTable(const QString& tableRef) const;

private:
    void report(const QString& message) const;

    QString m_projectRoot;
    DiagnosticCallback m_diagnosticCallback;
};
