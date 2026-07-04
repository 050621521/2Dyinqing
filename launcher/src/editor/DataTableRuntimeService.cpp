#include "DataTableRuntimeService.h"
#include "models/AssetRegistry.h"

DataTableRuntimeService::DataTableRuntimeService(QString projectRoot)
    : m_projectRoot(std::move(projectRoot)) {}

QString DataTableRuntimeService::resolvePath(const QString& tableRef) const {
    AssetRegistry registry(m_projectRoot);
    registry.load();
    return AssetResolver(m_projectRoot, &registry)
        .resolve(SoftAssetRef::fromString(tableRef, "datatable"), "DataTables", ".datatable");
}

bool DataTableRuntimeService::load(const QString& tableRef, DataTable* outTable) const {
    if (!outTable) return false;
    const QString path = resolvePath(tableRef);
    if (!outTable->load(path)) {
        report(QString("数据表加载失败：%1").arg(path));
        return false;
    }
    return true;
}

BPValue DataTableRuntimeService::fieldValue(const QString& tableRef, const QString& recordId, const QString& fieldName) const {
    DataTable table;
    if (!load(tableRef, &table)) return {};
    return table.value(recordId, fieldName);
}

int DataTableRuntimeService::recordCount(const QString& tableRef) const {
    DataTable table;
    if (!load(tableRef, &table)) return 0;
    return table.recordCount();
}

QList<BattleCard> DataTableRuntimeService::cardsFromTable(const QString& tableRef) const {
    DataTable table;
    if (!load(tableRef, &table)) return {};
    const QStringList schemaErrors = table.validateSchema("CardTable", m_projectRoot);
    if (!schemaErrors.isEmpty()) {
        report(QString("卡牌表结构无效：%1").arg(schemaErrors.join("；")));
        return {};
    }

    QList<BattleCard> cards;
    for (const DataTableRecord& r : table.records) {
        BattleCard c;
        c.id = r.values.value("id").toString(r.id);
        c.name = r.values.value("name").toString(c.id);
        c.description = r.values.value("description").toString();
        const SoftAssetRef effectRef = SoftAssetRef::fromVariant(r.values.value("effect"), "bp.effect");
        c.effect = effectRef.path.isEmpty() ? effectRef.assetId : effectRef.path;
        c.target = r.values.value("target").toString("enemy");
        c.cost = r.values.value("energy_cost").toInt();
        c.value = r.values.value("effect_value").toInt();
        if (!c.id.isEmpty()) cards << c;
    }
    return cards;
}

void DataTableRuntimeService::report(const QString& message) const {
    if (m_diagnosticCallback && !message.isEmpty())
        m_diagnosticCallback(message);
}
