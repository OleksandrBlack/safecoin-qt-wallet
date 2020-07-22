#ifndef RPCCLIENT_H
#define RPCCLIENT_H

#include "precompiled.h"

#include "balancestablemodel.h"
#include "txtablemodel.h"
#include "ui_mainwindow.h"
#include "mainwindow.h"
#include "connection.h"

class Turnstile;

struct TransactionItem {
    QString         type;
    qint64          datetime;
    QString         address;
    QString         txid;
    double          amount;
    long            confirmations;
    QString         fromAddr;
    QString         memo;
};

struct WatchedTx {
    QString opid;
    Tx tx;
    std::function<void(QString, QString)> completed;
    std::function<void(QString, QString)> error;
};

struct MigrationStatus {
    bool            available;     // Whether the underlying zcashd supports migration?
    bool            enabled;
    QString         saplingAddress;
    double          unmigrated;
    double          migrated;
    QList<QString>  txids;
};

class RPC
{
public:
    RPC(MainWindow* main);
    ~RPC();

    void setConnection(Connection* c);
    void setEZcashd(std::shared_ptr<QProcess> p);
    const QProcess* getEZcashD() { return ezcashd.get(); }

    void refresh(bool force = false);

    void refreshAddresses();    
    
    void checkForUpdate(bool silent = true);
    void refreshPrice();
    void getZboardTopics(std::function<void(QMap<QString, QString>)> cb);
	
    void executeStandardUITransaction(Tx tx); 

    void executeTransaction(Tx tx, 
        const std::function<void(QString opid)> submitted,
        const std::function<void(QString opid, QString txid)> computed,
        const std::function<void(QString opid, QString errStr)> error);

    void fillTxJsonParams(QJsonArray& params, Tx tx);
    void sendZTransaction(QJsonValue params, const std::function<void(QJsonValue)>& cb, const std::function<void(QString)>& err);
    void watchTxStatus();

    const QMap<QString, WatchedTx> getWatchingTxns() { return watchingOps; }
    void addNewTxToWatch(const QString& newOpid, WatchedTx wtx); 

    const TxTableModel*               getTransactionsModel() { return transactionsTableModel; }
    const QList<QString>*             getAllZAddresses()     { return zaddresses; }
    const QList<QString>*             getAllTAddresses()     { return taddresses; }
    const QList<UnspentOutput>*       getUTXOs()             { return utxos; }
    const QMap<QString, double>*      getAllBalances()       { return allBalances; }
    const QMap<QString, bool>*        getUsedAddresses()     { return usedAddresses; }

    void newZaddr(const std::function<void(QJsonValue)>& cb);
    void newTaddr(const std::function<void(QJsonValue)>& cb);

    void getZPrivKey(QString addr, const std::function<void(QJsonValue)>& cb);
    void getZViewKey(QString addr, const std::function<void(QJsonValue)>& cb);
    void getTPrivKey(QString addr, const std::function<void(QJsonValue)>& cb);
    void importZPrivKey(QString addr, bool rescan, const std::function<void(QJsonValue)>& cb);
    void importTPrivKey(QString addr, bool rescan, const std::function<void(QJsonValue)>& cb);
    void validateAddress(QString address, const std::function<void(QJsonValue)>& cb);

    void shutdownZcashd();
    void noConnection();
    bool isEmbedded() { return ezcashd != nullptr; }

    QString getDefaultSaplingAddress();
    QString getDefaultTAddress();

    void getAllPrivKeys(const std::function<void(QList<QPair<QString, QString>>)>);

    Turnstile*  getTurnstile()  { return turnstile; }
    Connection* getConnection() { return conn; }

    const MigrationStatus*      getMigrationStatus() { return &migrationStatus; }
    void                        setMigrationStatus(bool enabled);

private:
    void refreshBalances();

    void refreshTransactions();    
    void refreshMigration();
    void refreshSentZTrans();
    void refreshReceivedZTrans(QList<QString> zaddresses);

    bool processUnspent     (const QJsonValue& reply, QMap<QString, double>* newBalances, QList<UnspentOutput>* newUtxos);
    void updateUI           (bool anyUnconfirmed);

    void getInfoThenRefresh(bool force);

    void getBalance(const std::function<void(QJsonValue)>& cb);
    QJsonValue makePayload(QString method, QString params);
    QJsonValue makePayload(QString method);

    void getTransparentUnspent  (const std::function<void(QJsonValue)>& cb);
    void getZUnspent            (const std::function<void(QJsonValue)>& cb);
    void getTransactions        (const std::function<void(QJsonValue)>& cb);
    void getZAddresses          (const std::function<void(QJsonValue)>& cb);
    void getTAddresses          (const std::function<void(QJsonValue)>& cb);


    Connection*                 conn                        = nullptr;
    std::shared_ptr<QProcess>   ezcashd                     = nullptr;

    QList<UnspentOutput>*       utxos                       = nullptr;
    QMap<QString, double>*      allBalances                 = nullptr;
    QMap<QString, bool>*        usedAddresses               = nullptr;
    QList<QString>*             zaddresses                  = nullptr;
    QList<QString>*             taddresses                  = nullptr;
    
    QMap<QString, WatchedTx>    watchingOps;

    TxTableModel*               transactionsTableModel      = nullptr;
    BalancesTableModel*         balancesTableModel          = nullptr;

    QTimer*                     timer;
    QTimer*                     txTimer;
    QTimer*                     priceTimer;

    Ui::MainWindow*             ui;
    MainWindow*                 main;
    Turnstile*                  turnstile;

    // Sapling turnstile migration status (for the zcashd v2.0.5 tool)
    MigrationStatus             migrationStatus;

    // Current balance in the UI. If this number updates, then refresh the UI
    QString                     currentBalance;
};

#endif // RPCCLIENT_H
