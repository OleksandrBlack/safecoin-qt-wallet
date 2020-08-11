// Copyright 2019-2020 The Hush Developers
// 2020 Safecoin Developers
// Released under the GPLv3

#include "rpc.h"

#include "addressbook.h"
#include "settings.h"
#include "senttxstore.h"
#include "version.h"
#include "websockets.h"



RPC::RPC(MainWindow* main) {
    auto cl = new ConnectionLoader(main, this);

    // Execute the load connection async, so we can set up the rest of RPC properly. 
    QTimer::singleShot(1, [=]() { cl->loadConnection(); });

    this->main = main;
    this->ui = main->ui;

    // Setup balances table model
    balancesTableModel = new BalancesTableModel(main->ui->balancesTable);
    main->ui->balancesTable->setModel(balancesTableModel);

    // Setup transactions table model
    transactionsTableModel = new TxTableModel(ui->transactionsTable);
    main->ui->transactionsTable->setModel(transactionsTableModel);
    
    // Set up timer to refresh Price
    priceTimer = new QTimer(main);
    QObject::connect(priceTimer, &QTimer::timeout, [=]() {
        refreshPrice();
    });
    priceTimer->start(Settings::priceRefreshSpeed);  // Every hour

    // Set up a timer to refresh the UI every few seconds
    timer = new QTimer(main);
    QObject::connect(timer, &QTimer::timeout, [=]() {
        refresh();
    });
    timer->start(Settings::updateSpeed);    

    // Set up the timer to watch for tx status
    txTimer = new QTimer(main);
    QObject::connect(txTimer, &QTimer::timeout, [=]() {
        watchTxStatus();
    });
    // Start at every 10s. When an operation is pending, this will change to every second
    txTimer->start(Settings::updateSpeed);  

    usedAddresses = new QMap<QString, bool>();

    // Initialize the migration status to unavailable.
    this->migrationStatus.available = false;
}

RPC::~RPC() {
    delete timer;
    delete txTimer;

    delete transactionsTableModel;
    delete balancesTableModel;

    delete utxos;
    delete allBalances;
    delete usedAddresses;
    delete zaddresses;
    delete taddresses;

    delete conn;
}

void RPC::setEZcashd(std::shared_ptr<QProcess> p) {
    ezcashd = p;

    if ((ezcashd && ui->tabWidget->widget(4) == nullptr) && (ezcashd && ui->tabWidget->widget(5) == nullptr)) {
        ui->tabWidget->addTab(main->safenodestab, "SafeNodes") && ui->tabWidget->addTab(main->safecoindtab, "safecoind");
    }
}

// Called when a connection to safecoind is available. 
void RPC::setConnection(Connection* c) {
    if (c == nullptr) return;

    delete conn;
    this->conn = c;

    ui->statusBar->showMessage("Ready! Thank you for helping secure the Safecoin network by running a full node.");

    // See if we need to remove the reindex/rescan flags from the safecoin.conf file
    auto zcashConfLocation = Settings::getInstance()->getZcashdConfLocation();
    Settings::removeFromZcashConf(zcashConfLocation, "rescan");
    Settings::removeFromZcashConf(zcashConfLocation, "reindex");

    // Refresh the UI
    refreshPrice();
    checkForUpdate();

    // Force update, because this might be coming from a settings update
    // where we need to immediately refresh
    refresh(true);
}

QJsonValue RPC::makePayload(QString method, QString params) {
    QJsonObject payload = {
        {"jsonrpc", "1.0"},
        {"id", "42" },
        {"method", method },
        {"params", QJsonArray {params}}
    };
    return payload;
}

QJsonValue RPC::makePayload(QString method) {
    QJsonObject payload = {
        {"jsonrpc", "1.0"},
        {"id", "42" },
        {"method", method },
    };
    return payload;
}

void RPC::getTAddresses(const std::function<void(QJsonValue)>& cb) {
    QString method = "getaddressesbyaccount";
    //    QString params = "";   // We're removing the params to get all addresses, similar to z_listaddresses for Z
    conn->doRPCWithDefaultErrorHandling(makePayload(method, ""), cb);
}

void RPC::getZAddresses(const std::function<void(QJsonValue)>& cb) {
    QString method = "z_listaddresses";
    conn->doRPCWithDefaultErrorHandling(makePayload(method), cb);
}

void RPC::getTransparentUnspent(const std::function<void(QJsonValue)>& cb) {
    QJsonObject payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "listunspent"},
        {"params", QJsonArray {0}}             // Get UTXOs with 0 confirmations as well.
    };

    conn->doRPCWithDefaultErrorHandling(payload, cb);
}

void RPC::getZUnspent(const std::function<void(QJsonValue)>& cb) {
    QJsonObject payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "z_listunspent"},
        {"params", QJsonArray {0}}             // Get UTXOs with 0 confirmations as well.
    };

    conn->doRPCWithDefaultErrorHandling(payload, cb);
}

void RPC::newZaddr(const std::function<void(QJsonValue)>& cb) {
    QJsonObject payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "z_getnewaddress"},
        {"params", QJsonArray { "sapling" }},
    };
    
    conn->doRPCWithDefaultErrorHandling(payload, cb);
}


void RPC::newTaddr(const std::function<void(QJsonValue)>& cb) {
    QString method = "getnewaddress";
    conn->doRPCWithDefaultErrorHandling(makePayload(method), cb);
}

void RPC::getZViewKey(QString addr, const std::function<void(QJsonValue)>& cb) {
    QString method = "z_exportviewingkey";
    conn->doRPCWithDefaultErrorHandling(makePayload(method, addr), cb);
}

void RPC::getZPrivKey(QString addr, const std::function<void(QJsonValue)>& cb) {
    QString method = "z_exportkey";
    conn->doRPCWithDefaultErrorHandling(makePayload(method, addr), cb);
}

void RPC::getTPrivKey(QString addr, const std::function<void(QJsonValue)>& cb) {
    QString method = "dumpprivkey";
    conn->doRPCWithDefaultErrorHandling(makePayload(method, addr), cb);
}

void RPC::importZPrivKey(QString privkey, bool rescan, const std::function<void(QJsonValue)>& cb) {
    QJsonObject payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "z_importkey"},
        {"params", QJsonArray { privkey, (rescan ? "yes" : "no") }},
    };
    
    conn->doRPCWithDefaultErrorHandling(payload, cb);
}


// TODO: support rescan height and prefix
void RPC::importTPrivKey(QString privkey, bool rescan, const std::function<void(QJsonValue)>& cb) {
    QJsonObject payload;

    // If privkey starts with 5, K or L, use old-style Hush params, same as BTC+ZEC
    if( privkey.startsWith("5") || privkey.startsWith("K") || privkey.startsWith("L") ) {
        qDebug() << "Detected old-style SAFECOIN WIF";
        payload = {
            {"jsonrpc", "1.0"},
            {"id", "someid"},
            {"method", "importprivkey"},
            {"params", QJsonArray { privkey, "" }},
        };
    } else {
        qDebug() << "Detected new-style SAFECOIN WIF";
        payload = {
            {"jsonrpc", "1.0"},
            {"id", "someid"},
            {"method", "importprivkey"},
            {"params", QJsonArray { privkey, "" }},   //likely remove this case in future
        };
    }

    qDebug() <<  "Importing WIF with rescan=" << rescan;

    conn->doRPCWithDefaultErrorHandling(payload, cb);
}

void RPC::validateAddress(QString address, const std::function<void(QJsonValue)>& cb) {
    QString method = address.startsWith("s") ? "z_validateaddress" : "validateaddress";
    conn->doRPCWithDefaultErrorHandling(makePayload(method, address), cb);
}

void RPC::getBalance(const std::function<void(QJsonValue)>& cb) {
    QJsonObject payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "z_gettotalbalance"},
        {"params", QJsonArray {0}}             // Get Unconfirmed balance as well.
    };

    conn->doRPCWithDefaultErrorHandling(payload, cb);
}

void RPC::getTransactions(const std::function<void(QJsonValue)>& cb) {
    QString method = "listtransactions";
    conn->doRPCWithDefaultErrorHandling(makePayload(method), cb);
}

void RPC::sendZTransaction(QJsonValue params, const std::function<void(QJsonValue)>& cb,
    const std::function<void(QString)>& err) {
    QJsonObject payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "z_sendmany"},
        {"params", params}
    };

    conn->doRPC(payload, cb,  [=] (QNetworkReply *reply, const QJsonValue &parsed) {
        if (!parsed.isUndefined() && !parsed["error"].toObject()["message"].isNull()) {
            err(parsed["error"].toObject()["message"].toString());
        } else {
            err(reply->errorString());
        }
    });
}

/**
 * Method to get all the private keys for both z and t addresses. It will make 2 batch calls,
 * combine the result, and call the callback with a single list containing both the t-addr and z-addr
 * private keys
 */ 
void RPC::getAllPrivKeys(const std::function<void(QList<QPair<QString, QString>>)> cb) {
    if (conn == nullptr) {
        // No connection, just return
        return;
    }

    auto callnum = new int; 
    // A special function that will call the callback when two lists have been added
    auto holder = new QPair<int, QList<QPair<QString, QString>>>();
    holder->first = 0;  // This is the number of times the callback has been called, initialized to 0
    auto fnCombineTwoLists = [=] (QList<QPair<QString, QString>> list) {
        // Increment the callback counter
        holder->first++;    

        // Add all
        std::copy(list.begin(), list.end(), std::back_inserter(holder->second));

        // And if the caller has been called twice, do the parent callback with the 
        // collected list
        if (holder->first == 3) {
            // Sort so z addresses are on top
            std::sort(holder->second.begin(), holder->second.end(), 
                        [=] (auto a, auto b) { return a.first > b.first; });

            cb(holder->second);
	    
            delete holder;
        }
	
    };

    // A utility fn to do the batch calling
    auto fnDoBatchGetPrivKeys = [=](QJsonValue getAddressPayload, QString privKeyDumpMethodName) {
        conn->doRPCIgnoreError(getAddressPayload, [=] (QJsonValue resp) {
            QList<QString> addrs;
            for (auto addr : resp.toArray()) {
                addrs.push_back(addr.toString());
            }

        if (addrs.isEmpty()){
	  holder->first++;
	  return;
	}
	  
	    // Then, do a batch request to get all the private keys
            conn->doBatchRPC<QString>(
                addrs, 
                [=] (auto addr) {
                    QJsonObject payload = {
                        {"jsonrpc", "1.0"},
                        {"id", "someid"},
                        {"method", privKeyDumpMethodName},
                        {"params", QJsonArray { addr }},
                    };
		    return payload;
                },
                [=] (QMap<QString, QJsonValue>* privkeys) {
                    QList<QPair<QString, QString>> allTKeys;
		        for (QString addr: privkeys->keys()) {
                        allTKeys.push_back(
                            QPair<QString, QString>(
                                addr, 
                                privkeys->value(addr).toString()));
                    }
                    fnCombineTwoLists(allTKeys);
                    delete privkeys;
                }
            );
        });
    };

    // First get all the t and z addresses.
    QJsonObject payloadT = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "getaddressesbyaccount"}
	//        {"params", QJsonArray {""} }    // We're removing params here in order to get addressesin all accounts, similar to z_listaddresses
    };

    // Unspent addresses.   Added because there are situations where the unspent address is not assigned to any account
    QJsonObject payloadU = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "listunspent"},
        {"params", QJsonArray {-2} }    // Simplified listunspent to return addresses for unset accounts                                                        
    };

    
    QJsonObject payloadZ = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "z_listaddresses"}
    };

    fnDoBatchGetPrivKeys(payloadT, "dumpprivkey");
    fnDoBatchGetPrivKeys(payloadU, "dumpprivkey");
    fnDoBatchGetPrivKeys(payloadZ, "z_exportkey");


}


// Build the RPC JSON Parameters for this tx
void RPC::fillTxJsonParams(QJsonArray& params, Tx tx) {

    Q_ASSERT(QJsonValue(params).isArray());

    // Get all the addresses and amounts
    QJsonArray allRecepients;

    // For each addr/amt/memo, construct the JSON and also build the confirm dialog box    
    for (int i=0; i < tx.toAddrs.size(); i++) {
        auto toAddr = tx.toAddrs[i];

        // Construct the JSON params
        QJsonObject rec;
        rec["address"]      = toAddr.addr;
        // Force it through string for rounding. Without this, decimal points beyond 8 places
        // will appear, causing an "invalid amount" error
        rec["amount"]       = Settings::getDecimalString(toAddr.amount); //.toDouble();
        if (Settings::isZAddress(toAddr.addr) && !toAddr.encodedMemo.trimmed().isEmpty())
            rec["memo"]     = toAddr.encodedMemo;

        allRecepients.push_back(rec);
    }

    // Add sender    
    params.push_back(tx.fromAddr);
    params.push_back(allRecepients);

    // Add fees if custom fees are allowed.
    if (Settings::getInstance()->getAllowCustomFees()) {
        params.push_back(1); // minconf
        params.push_back(tx.fee);
    }

}


void RPC::noConnection() {    
    QIcon i = QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical);
    main->statusIcon->setPixmap(i.pixmap(16, 16));
    main->statusIcon->setToolTip("");
    main->statusLabel->setText(QObject::tr("No Connection"));
    main->statusLabel->setToolTip("");
    main->ui->statusBar->showMessage(QObject::tr("No Connection"), 1000);

    // Clear balances table.
    QMap<QString, double> emptyBalances;
    QList<UnspentOutput>  emptyOutputs;
    balancesTableModel->setNewData(&emptyBalances, &emptyOutputs);

    // Clear Transactions table.
    QList<TransactionItem> emptyTxs;
    transactionsTableModel->addTData(emptyTxs);
    transactionsTableModel->addZRecvData(emptyTxs);
    transactionsTableModel->addZSentData(emptyTxs);

    // Clear balances
    ui->balSheilded->setText("");
    ui->balTransparent->setText("");
    ui->balTotal->setText("");
    ui->balUSDTotal->setText("");

    ui->balSheilded->setToolTip("");
    ui->balTransparent->setToolTip("");
    ui->balTotal->setToolTip("");
    ui->balUSDTotal->setToolTip("");

    // Clear send tab from address
    ui->inputsCombo->clear();
}

// Refresh received z txs by calling z_listreceivedbyaddress/gettransaction
void RPC::refreshReceivedZTrans(QList<QString> zaddrs) {
    if  (conn == nullptr) 
        return noConnection();

    // We'll only refresh the received Z txs if settings allows us.
    if (!Settings::getInstance()->getSaveZtxs()) {
        QList<TransactionItem> emptylist;
        transactionsTableModel->addZRecvData(emptylist);
        return;
    }
        
    // This method is complicated because z_listreceivedbyaddress only returns the txid, and 
    // we have to make a follow up call to gettransaction to get details of that transaction. 
    // Additionally, it has to be done in batches, because there are multiple z-Addresses, 
    // and each z-Addr can have multiple received txs. 

    // 1. For each z-Addr, get list of received txs    
    conn->doBatchRPC<QString>(zaddrs,
        [=] (QString zaddr) {
            QJsonObject payload = {
                {"jsonrpc", "1.0"},
                {"id", "z_lrba"},
                {"method", "z_listreceivedbyaddress"},
                {"params", QJsonArray {zaddr, 0}}      // Accept 0 conf as well.
            };

            return payload;
        },          
        [=] (QMap<QString, QJsonValue>* zaddrTxids) {
            // Process all txids, removing duplicates. This can happen if the same address
            // appears multiple times in a single tx's outputs.
            QSet<QString> txids;
            QMap<QString, QString> memos;
            for (auto it = zaddrTxids->constBegin(); it != zaddrTxids->constEnd(); it++) {
                auto zaddr = it.key();
                for (const auto& i : it.value().toArray()) {
                    // Mark the address as used
                    usedAddresses->insert(zaddr, true);

                    // Filter out change txs
                    if (! i.toObject()["change"].toBool()) {
                        auto txid = i.toObject()["txid"].toString();
                        txids.insert(txid);    

                        // Check for Memos
                        QString memoBytes = i.toObject()["memo"].toString();
                        if (!memoBytes.startsWith("f600"))  {
                            QString memo(QByteArray::fromHex(
                                            i.toObject()["memo"].toString().toUtf8()));
                            if (!memo.trimmed().isEmpty())
                                memos[zaddr + txid] = memo;
                        }
                    }
                }                        
            }

            // 2. For all txids, go and get the details of that txid.
            conn->doBatchRPC<QString>(txids.toList(),
                [=] (QString txid) {
                    QJsonObject payload = {
                        {"jsonrpc", "1.0"},
                        {"id",  "gettx"},
                        {"method", "gettransaction"},
                        {"params", QJsonArray {txid}}
                    };

                    return payload;
                },
                [=] (QMap<QString, QJsonValue>* txidDetails) {
                    QList<TransactionItem> txdata;

                    // Combine them both together. For every zAddr's txid, get the amount, fee, confirmations and time
                    for (auto it = zaddrTxids->constBegin(); it != zaddrTxids->constEnd(); it++) {                        
                        for (const auto& i : it.value().toArray()) {
                            // Filter out change txs
                            if (i.toObject()["change"].toBool())
                                continue;
                            
                            auto zaddr = it.key();
                            auto txid  = i.toObject()["txid"].toString();

                            // Lookup txid in the map
                            auto txidInfo = txidDetails->value(txid);

                            qint64 timestamp;
                            if (!txidInfo.toObject()["time"].isUndefined()) {
                                timestamp = txidInfo.toObject()["time"].toInt();
                            } else {
                                timestamp = txidInfo.toObject()["blocktime"].toInt();
                            }
                            
                            auto amount        = i.toObject()["amount"].toDouble();
                            auto confirmations = static_cast<unsigned long>(txidInfo["confirmations"].toInt());


                            TransactionItem tx{ QString("receive"), timestamp, zaddr, txid, amount, 
				static_cast<long>(confirmations), "", memos.value(zaddr + txid, "") };
                            txdata.push_front(tx);
                        }
                    }

                    transactionsTableModel->addZRecvData(txdata);

                    // Cleanup both responses;
                    delete zaddrTxids;
                    delete txidDetails;
                }
            );
        }
    );
} 


/// This will refresh all the balance data from safecoind
void RPC::refresh(bool force) {
    if  (conn == nullptr) 
        return noConnection();

    getInfoThenRefresh(force);
}


void RPC::getInfoThenRefresh(bool force) {
    if  (conn == nullptr) 
        return noConnection();

    static bool prevCallSucceeded = false;
    QString method = "getinfo";

    conn->doRPC(makePayload(method), [=] (const QJsonValue& reply) {
        prevCallSucceeded = true;
        // Testnet?
        if (!reply["testnet"].isNull()) {
            Settings::getInstance()->setTestnet(reply["testnet"].toBool());
        };

        // TODO: checkmark only when getinfo.synced == true!
        // Connected, so display checkmark.
        QIcon i(":/icons/res/connected.gif");
        main->statusIcon->setPixmap(i.pixmap(16, 16));

        static int lastBlock    = 0;

        int curBlock            = reply["blocks"].toInt();
        int longestchain        = reply["longestchain"].toInt();
        int version             = reply["version"].toInt();
        int p2pport             = reply["p2pport"].toInt();
        int rpcport             = reply["rpcport"].toInt();
        int notarized           = reply["notarized"].toInt();
        int protocolversion     = reply["protocolversion"].toInt();
        int tls_connections     = reply["tls_connections"].toInt();
        int lag                 = curBlock - notarized;
        int blocks_until_halving= 2207378 - curBlock;
        char halving_days[8];
        sprintf(halving_days, "%.2f", (double) (blocks_until_halving * 150) / (60*60*24) );
        QString ntzhash         = reply["notarizedhash"].toString();
        QString ntztxid         = reply["notarizedtxid"].toString();
        QString safever          = reply["SAFEversion"].toString();


        Settings::getInstance()->setZcashdVersion(version);

        ui->notarized->setText(QString::number(notarized));
        ui->longestchain->setText(QString::number(longestchain));
        ui->notarizedhashvalue->setText( ntzhash );
        ui->notarizedtxidvalue->setText( ntztxid );
        ui->version->setText( QString::number(version) );
        ui->safeversion->setText( safever );
        ui->protocolversion->setText( QString::number(protocolversion) );
        ui->tls_connections->setText( QString::number(tls_connections) );
        ui->p2pport->setText( QString::number(p2pport) );
        ui->rpcport->setText( QString::number(rpcport) );


        // See if recurring payments needs anything
        Recurring::getInstance()->processPending(main);

        if ( force || (curBlock != lastBlock) ) {
            // Something changed, so refresh everything.
            lastBlock = curBlock;

            refreshBalances();        
            refreshAddresses();     // This calls refreshZSentTransactions() and refreshReceivedZTrans()
            refreshTransactions();
	    //            refreshMigration();     // Sapling turnstile migration status.
        }

        int connections = reply["connections"].toInt();
        Settings::getInstance()->setPeers(connections);

        if (connections == 0) {
            // If there are no peers connected, then the internet is probably off or something else is wrong. 
            QIcon i = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
            main->statusIcon->setPixmap(i.pixmap(16, 16));
        }

        // Get network sol/s
        QJsonObject payload = {
            {"jsonrpc", "1.0"},
            {"id", "someid"},
            {"method", "getnetworksolps"}
        };


        QString method = "getnetworksolps";
        conn->doRPCIgnoreError(makePayload(method), [=](const QJsonValue& reply) {
            qint64 solrate = reply.toInt();

		
                ui->numconnections->setText(QString::number(connections));
                ui->solrate->setText(QString::number(solrate) % " Sol/s");
            });

        // Get activenodes
        payload = {
            {"jsonrpc", "1.0"},
            {"id", "someid"},
            {"method", "getactivenodes"}
        };
        conn->doRPCIgnoreError(payload, [=] (const QJsonValue& reply) {
            double collateral_total;
            int node_count          = reply["node_count"].toInt();
            int tier_0_count        = reply["tier_0_count"].toInt();
            int tier_1_count        = reply["tier_1_count"].toInt();
            int tier_2_count        = reply["tier_2_count"].toInt();
            int tier_3_count        = reply["tier_3_count"].toInt();
            collateral_total    = reply["collateral_total"].toDouble();

            ui->node_count->setText(QString::number(node_count));
			
		if (!getConnection()->config->addrindex.isEmpty()) {
			
            ui->tier_0_count->setText(QString::number(tier_0_count));
            ui->tier_1_count->setText(QString::number(tier_1_count));
            ui->tier_2_count->setText(QString::number(tier_2_count));
            ui->tier_3_count->setText(QString::number(tier_3_count));
            ui->collateral_total->setToolTip(Settings::getDisplayFormat(collateral_total));
            ui->collateral_total->setText(Settings::getDisplayFormat(collateral_total));
            ui->collateral_total_usd->setToolTip(Settings::getUSDFormat(collateral_total));
            ui->collateral_total_usd->setText(Settings::getUSDFormat(collateral_total));

		} else {
				ui->tier_0_count->setText("addressindex not enabled");
				ui->tier_1_count->setText("addressindex not enabled");
				ui->tier_2_count->setText("addressindex not enabled");
				ui->tier_3_count->setText("addressindex not enabled");
				ui->collateral_total->setText("addressindex not enabled");
				ui->collateral_total_usd->setText("addressindex not enabled");
		}


        });


        // Get nodeinfo
        payload = {
            {"jsonrpc", "1.0"},
            {"id", "someid"},
            {"method", "getnodeinfo"}
        };
        conn->doRPCIgnoreError(payload, [=] (const QJsonValue& reply) {
		
		double balance, collateral;
		int tier;
		int last_reg_height;
		int valid_thru_height;
		bool is_valid;

	if (!getConnection()->config->confsnode.isEmpty()) {
		if (!getConnection()->config->addrindex.isEmpty()) {
			try
			{
			  balance = reply["balance"].toDouble();
				
				ui->balance->setToolTip(Settings::getDisplayFormat(balance));
				ui->balance->setText(Settings::getDisplayFormat(balance));
				ui->balance_usd->setToolTip(Settings::getUSDFormat(balance));
				ui->balance_usd->setText(Settings::getUSDFormat(balance));
			}
			catch (...)
			{
				ui->balance->setText("unknown");
				ui->balance_usd->setText("unknown");
			}
			try
			{
			  collateral = reply["collateral"].toDouble();
				
				ui->collateral->setToolTip(Settings::getDisplayFormat(collateral));
				ui->collateral->setText(Settings::getDisplayFormat(collateral));
				ui->collateral_usd->setToolTip(Settings::getUSDFormat(collateral));
				ui->collateral_usd->setText(Settings::getUSDFormat(collateral));
			}
			catch (...)
			{
				ui->collateral->setText("unknown");
				ui->collateral_usd->setText("unknown");
			}
		
			
			try
			{
				tier = reply["tier"].toInt();
				
				ui->tier->setText(QString::number(tier));
			}
			catch (...)
			{
				ui->tier->setText("unknown");
			}
		} else {
				ui->balance->setText("addressindex not enabled");
				ui->balance_usd->setText("addressindex not enabled");
				ui->collateral->setText("addressindex not enabled");
				ui->collateral_usd->setText("addressindex not enabled");
				ui->tier->setText("addressindex not enabled");
		}

			is_valid = reply["is_valid"].toBool();

			QString error_line;

			error_line = reply["errors"].toString();
			
			//			for (unsigned int i = 0; i < vs_errors.size(); i++)
			
			//{
			//	error_line = error_line + QString(vs_errors.at(i).c_str()) + "\n";
			//}
			
			ui->is_valid->setText(is_valid?"YES":"NO");
			ui->errors->setText(error_line);


		if (is_valid == true) {
			try
			{
				last_reg_height = reply["last_reg_height"].toInt();
				
				ui->last_reg_height->setText(QString::number(last_reg_height));
			}
			catch (...)
			{
				ui->last_reg_height->setText("unknown");
			}
			try
			{
				valid_thru_height = reply["valid_thru_height"].toInt();
				
				ui->valid_thru_height->setText(QString::number(valid_thru_height));
			}
			catch (...)
			{
				ui->valid_thru_height->setText("unknown");
			}
		} else {
			ui->last_reg_height->setText("not valid");
			ui->valid_thru_height->setText("not valid");
		}

		
		QString parentkey   = QString::fromStdString( reply["parentkey"].toString().toStdString() );
			QString safekey     = QString::fromStdString( reply["safekey"].toString().toStdString() );
			QString safeheight  = QString::fromStdString( reply["safeheight"].toString().toStdString() );
			QString SAFE_address  = QString::fromStdString( reply["SAFE_address"].toString().toStdString() );
			
			ui->parentkey->setText(parentkey);
			ui->safekey->setText(safekey);
			ui->safeheight->setText(safeheight);
			ui->safeaddress->setText(SAFE_address);
	} else {
			ui->balance->setText("not configured");
			ui->balance_usd->setText("not configured");
			ui->collateral->setText("not configured");
			ui->collateral_usd->setText("not configured");
			ui->tier->setText("not configured");
			ui->is_valid->setText("not configured");
			ui->errors->setText("not configured");
			ui->last_reg_height->setText("not configured");
			ui->valid_thru_height->setText("not configured");
			ui->parentkey->setText("not configured");
			ui->safekey->setText("not configured");
			ui->safeheight->setText("not configured");
			ui->safeaddress->setText("not configured");
	}
        });


        // Get network info
        payload = {
            {"jsonrpc", "1.0"},
            {"id", "someid"},
            {"method", "getnetworkinfo"}
        };

        conn->doRPCIgnoreError(payload, [=](const QJsonValue& reply) {
            QString clientname    = reply["subversion"].toString();
            QString localservices = reply["localservices"].toString();


            ui->clientname->setText(clientname);
            ui->localservices->setText(localservices);
        });


        conn->doRPCIgnoreError(makePayload("getwalletinfo"), [=](const QJsonValue& reply) {
            int  txcount = reply["txcount"].toInt();
            ui->txcount->setText(QString::number(txcount));
        });

        //TODO: If -zindex is enabled, show stats
        conn->doRPCIgnoreError(makePayload("getchaintxstats"), [=](const QJsonValue& reply) {
            int  txcount = reply["txcount"].toInt();
            ui->chaintxcount->setText(QString::number(txcount));
        });

        // Call to see if the blockchain is syncing. 
        conn->doRPCIgnoreError(makePayload("getblockchaininfo"), [=](const QJsonValue& reply) {
            auto progress    = reply["verificationprogress"].toDouble();
            // TODO: use getinfo.synced
            bool isSyncing   = progress < 0.9999; // 99.99%
            int  blockNumber = reply["blocks"].toInt();

            int estimatedheight = 0;
            if (!reply.toObject()["estimatedheight"].isUndefined()) {
                estimatedheight = reply["estimatedheight"].toInt();
            }

            auto s = Settings::getInstance();
            s->setSyncing(isSyncing);
            s->setBlockNumber(blockNumber);
            QString ticker = s->get_currency_name();

            // Update safecoind tab
            if (isSyncing) {
                QString txt = QString::number(blockNumber);
                if (estimatedheight > 0) {
                    txt = txt % " / ~" % QString::number(estimatedheight);
                    // If estimated height is available, then use the download blocks 
                    // as the progress instead of verification progress.
                    progress = (double)blockNumber / (double)estimatedheight;
                }
                txt = txt %  " ( " % QString::number(progress * 100, 'f', 2) % "% )";
                ui->blockheight->setText(txt);
                ui->heightLabel->setText(QObject::tr("Downloading blocks"));
            } else {
                ui->blockheight->setText(QString::number(blockNumber));
                ui->heightLabel->setText(QObject::tr("Block height"));
            }

            auto ticker_price = s->get_price(ticker);

            QString extra = "";
            if(ticker_price > 0 && ticker != "BTC") {
                extra = QString::number( s->getBTCPrice() ) % "sat";
            }
            QString price = "";
            if (ticker_price > 0) {
                price = QString(", ") % "SAFE" % "=" % QString::number( (double)ticker_price,'f',8) % " " % ticker % " " % extra;
            }

            // Update the status bar
            QString statusText = QString() %
                (isSyncing ? QObject::tr("Syncing") : QObject::tr("Connected")) %
                " (" %
                (s->isTestnet() ? QObject::tr("testnet:") : "") %
                QString::number(blockNumber) %
                (isSyncing ? ("/" % QString::number(progress*100, 'f', 2) % "%") : QString()) %
                ") " %
                " Lag: " % QString::number(blockNumber - notarized) % price;
            main->statusLabel->setText(statusText);


            // Update the balances view to show a warning if the node is still syncing
            ui->lblSyncWarning->setVisible(isSyncing);
            ui->lblSyncWarningReceive->setVisible(isSyncing);

            auto safePrice = Settings::getInstance()->getUSDFormat(1);
            QString tooltip;
            if (connections > 0) {
                tooltip = QObject::tr("Connected to safecoind");
            }
            else {
                tooltip = QObject::tr("safecoind has no peer connections");
            }
            tooltip = tooltip % "(v" % QString::number(Settings::getInstance()->getZcashdVersion()) % ")";

            if (!safePrice.isEmpty()) {
                tooltip = "1 SAFE = " % safePrice % "\n" % tooltip;

            }
            main->statusLabel->setToolTip(tooltip);
            main->statusIcon->setToolTip(tooltip);
        });


    }, [=](QNetworkReply* reply, const QJsonValue&) {
        // safecoind has probably disappeared.

        this->noConnection();

        // Prevent multiple dialog boxes, because these are called async
        static bool shown = false;
        if (!shown && prevCallSucceeded) { // show error only first time
            shown = true;
            QMessageBox::critical(main, QObject::tr("Connection Error"), QObject::tr("There was an error connecting to safecoind. The error was") + ": \n\n"
                + reply->errorString(), QMessageBox::StandardButton::Ok);
            shown = false;
        }

        prevCallSucceeded = false;
    });
}

void RPC::refreshAddresses() {
    if  (conn == nullptr) 
        return noConnection();
    
    auto newzaddresses = new QList<QString>();

    getZAddresses([=] (QJsonValue reply) {
        for (const auto& it : reply.toArray()) {
            auto addr = it.toString();
            newzaddresses->push_back(addr);
        }

        delete zaddresses;
        zaddresses = newzaddresses;

        // Refresh the sent and received txs from all these z-addresses
        refreshSentZTrans();
        refreshReceivedZTrans(*zaddresses);
    });

    
    auto newtaddresses = new QList<QString>();
    getTAddresses([=] (QJsonValue reply) {
        for (const auto& it : reply.toArray()) {
            auto addr = it.toString();
            if (Settings::isTAddress(addr))
                newtaddresses->push_back(addr);
        }

        delete taddresses;
        taddresses = newtaddresses;

        // If there are no t Addresses, create one
	//        newTaddr([=] (json reply) {
            // What if taddress gets deleted before this executes?
        //    taddresses->append(QString::fromStdString(reply.get<json::string_t>()));
        //});
    });
}

// Function to create the data model and update the views, used below.
void RPC::updateUI(bool anyUnconfirmed) {    

    ui->unconfirmedWarning->setVisible(anyUnconfirmed);
	
	// Sending button are hidden until complete synchronization or transaction confirmation
    if (anyUnconfirmed == true) {
		ui->sendTransactionButton->setVisible(false);
		ui->BlocksendingWarning->setVisible(true);
	} else {
		ui->sendTransactionButton->setVisible(true);
		ui->BlocksendingWarning->setVisible(false);
	} 
		
    // Update balances model data, which will update the table too
    balancesTableModel->setNewData(allBalances, utxos);

    // Update from address
    main->updateFromCombo();
};

// Function to process reply of the listunspent and z_listunspent API calls, used below.
bool RPC::processUnspent(const QJsonValue& reply, QMap<QString, double>* balancesMap, QList<UnspentOutput>* newUtxos) {
    bool anyUnconfirmed = false;
    for (const auto& it : reply.toArray()) {
        QString qsAddr = it.toObject()["address"].toString();
        auto confirmations = it.toObject()["confirmations"].toInt();
        if (confirmations == 0) {
            anyUnconfirmed = true;
        }

        newUtxos->push_back(
            UnspentOutput{ qsAddr, it.toObject()["txid"].toString(),
                            Settings::getDecimalString(it.toObject()["amount"].toDouble()),
                            (int)confirmations, it.toObject()["spendable"].toBool() });

        (*balancesMap)[qsAddr] = (*balancesMap)[qsAddr] + it.toObject()["amount"].toDouble();
    }
    return anyUnconfirmed;
};



void RPC::refreshBalances() {    
    if  (conn == nullptr) 
        return noConnection();

    // 1. Get the Balances
    getBalance([=] (QJsonValue reply) {

        auto balT      = reply["transparent"].toString().toDouble();
        auto balZ      = reply["private"].toString().toDouble();
        auto balTotal  = reply["total"].toString().toDouble();


        AppDataModel::getInstance()->setBalances(balT, balZ);

        ui->balSheilded   ->setText(Settings::getDisplayFormat(balZ));
        ui->balTransparent->setText(Settings::getDisplayFormat(balT));
        ui->balTotal      ->setText(Settings::getDisplayFormat(balTotal));


        ui->balSheilded   ->setToolTip(Settings::getDisplayFormat(balZ));
        ui->balTransparent->setToolTip(Settings::getDisplayFormat(balT));
        ui->balTotal      ->setToolTip(Settings::getDisplayFormat(balTotal));

        ui->balUSDTotal      ->setText(Settings::getUSDFormat(balTotal));
        ui->balUSDTotal      ->setToolTip(Settings::getUSDFormat(balTotal));
    });

    // 2. Get the UTXOs
    // First, create a new UTXO list. It will be replacing the existing list when everything is processed.
    auto newUtxos = new QList<UnspentOutput>();
    auto newBalances = new QMap<QString, double>();

    // Call the Transparent and Z unspent APIs serially and then, once they're done, update the UI
    getTransparentUnspent([=] (QJsonValue reply) {
        auto anyTUnconfirmed = processUnspent(reply, newBalances, newUtxos);

        getZUnspent([=] (QJsonValue reply) {
            auto anyZUnconfirmed = processUnspent(reply, newBalances, newUtxos);

            // Swap out the balances and UTXOs
            delete allBalances;
            delete utxos;

            allBalances = newBalances;
            utxos       = newUtxos;

            updateUI(anyTUnconfirmed || anyZUnconfirmed);

            main->balancesReady();
        });        
    });
}

void RPC::refreshTransactions() {    
    if  (conn == nullptr) 
        return noConnection();

    getTransactions([=] (QJsonValue reply) {
        QList<TransactionItem> txdata;

        for (const auto& it : reply.toArray()) {
            double fee = 0;
            if (!it.toObject()["fee"].isNull()) {
                fee = it.toObject()["fee"].toDouble();
            }

            QString address = (it.toObject()["address"].isNull() ? "" : it.toObject()["address"].toString());

            TransactionItem tx{
                it.toObject()["category"].toString(),
                (qint64)it.toObject()["time"].toInt(),
                address,
                it.toObject()["txid"].toString(),
                it.toObject()["amount"].toDouble() + fee,
		  static_cast<long>(it.toObject()["confirmations"].toInt()),
                "", "" };

            txdata.push_back(tx);
            if (!address.isEmpty())
                usedAddresses->insert(address, true);
        }

        // Update model data, which updates the table view
        transactionsTableModel->addTData(txdata);        
    });
}

// Read sent Z transactions from the file.
void RPC::refreshSentZTrans() {
    if  (conn == nullptr) 
        return noConnection();

    auto sentZTxs = SentTxStore::readSentTxFile();

    // If there are no sent z txs, then empty the table. 
    // This happens when you clear history.
    if (sentZTxs.isEmpty()) {
        transactionsTableModel->addZSentData(sentZTxs);
        return;
    }

    QList<QString> txids;

    for (auto sentTx: sentZTxs) {
        txids.push_back(sentTx.txid);
    }

    // Look up all the txids to get the confirmation count for them. 
    conn->doBatchRPC<QString>(txids,
        [=] (QString txid) {
            QJsonObject payload = {
                {"jsonrpc", "1.0"},
                {"id", "senttxid"},
                {"method", "gettransaction"},
                {"params", QJsonArray {txid}}
            };

            return payload;
        },          
        [=] (QMap<QString, QJsonValue>* txidList) {
            auto newSentZTxs = sentZTxs;
            // Update the original sent list with the confirmation count
            // TODO: This whole thing is kinda inefficient. We should probably just update the file
            // with the confirmed block number, so we don't have to keep calling gettransaction for the
            // sent items.
            for (TransactionItem& sentTx: newSentZTxs) {
                auto j = txidList->value(sentTx.txid);
                if (j.isNull())
                    continue;
                auto error = j["confirmations"].isNull();
                if (!error)
                    sentTx.confirmations = j["confirmations"].toInt();
            }
            
            transactionsTableModel->addZSentData(newSentZTxs);
            delete txidList;
        }
     );
}

void RPC::addNewTxToWatch(const QString& newOpid, WatchedTx wtx) {    
    watchingOps.insert(newOpid, wtx);

    watchTxStatus();
}

/**
 * Execute a transaction with the standard UI. i.e., standard status bar message and standard error
 * handling
 */
void RPC::executeStandardUITransaction(Tx tx) {
    executeTransaction(tx, 
        [=] (QString opid) {
            ui->statusBar->showMessage(QObject::tr("Computing Tx: ") % opid);
        },
        [=] (QString, QString txid) { 
            ui->statusBar->showMessage(Settings::txidStatusMessage + " " + txid);
        },
        [=] (QString opid, QString errStr) {
            ui->statusBar->showMessage(QObject::tr(" Tx ") % opid % QObject::tr(" failed"), 15 * 1000);

            if (!opid.isEmpty())
                errStr = QObject::tr("The transaction with id ") % opid % QObject::tr(" failed. The error was") + ":\n\n" + errStr; 

            QMessageBox::critical(main, QObject::tr("Transaction Error"), errStr, QMessageBox::Ok);            
        }
    );
}

// Execute a transaction!
void RPC::executeTransaction(Tx tx, 
        const std::function<void(QString opid)> submitted,
        const std::function<void(QString opid, QString txid)> computed,
        const std::function<void(QString opid, QString errStr)> error) {
    // First, create the json params
    QJsonArray params;
    fillTxJsonParams(params, tx);
    //std::cout << std::setw(2) << params << std::endl;

    sendZTransaction(params, [=](const QJsonValue& reply) {
        QString opid = reply.toString();

        // And then start monitoring the transaction
        addNewTxToWatch( opid, WatchedTx { opid, tx, computed, error} );
        submitted(opid);
    },
    [=](QString errStr) {
        error("", errStr);
    });
}


void RPC::watchTxStatus() {
    if  (conn == nullptr) 
        return noConnection();

    // Make an RPC to load pending operation statues
    conn->doRPCIgnoreError(makePayload("z_getoperationstatus"), [=] (const QJsonValue& reply) {
        // conn->doRPCIgnoreError(payload, [=] (const json& reply) {
        // There's an array for each item in the status
        for (const auto& it : reply.toArray()) {
            // If we were watching this Tx and its status became "success", then we'll show a status bar alert
            QString id = it.toObject()["id"].toString();
            if (watchingOps.contains(id)) {
                // log any txs we are watching
                //   "creation_time": 1515969376,
                // "execution_secs": 50.416337,
                // And if it ended up successful
                QString status = it.toObject()["status"].toString();
                main->loadingLabel->setVisible(false);

                if (status == "success") {
                    auto txid = it.toObject()["result"].toObject()["txid"].toString();
                    SentTxStore::addToSentTx(watchingOps[id].tx, txid);

                    auto wtx = watchingOps[id];
                    watchingOps.remove(id);
                    wtx.completed(id, txid);

                    qDebug() << "opid "<< id << " started at "<<QString::number((unsigned int)it.toObject()["creation_time"].toInt()) << " took " << QString::number((double)it.toObject()["execution_secs"].toDouble()) << " seconds";


                    refresh(true);
                } else if (status == "failed") {
                    // If it failed, then we'll actually show a warning.
                    auto errorMsg = it.toObject()["error"].toObject()["message"].toString();

                    auto wtx = watchingOps[id];
                    watchingOps.remove(id);
                    wtx.error(id, errorMsg);
                } 
            }

            if (watchingOps.isEmpty()) {
                txTimer->start(Settings::updateSpeed);
            } else {
                txTimer->start(Settings::quickUpdateSpeed);
            }
        }

        // If there is some op that we are watching, then show the loading bar, otherwise hide it
        if (watchingOps.empty()) {
            main->loadingLabel->setVisible(false);
        } else {
            main->loadingLabel->setVisible(true);
            main->loadingLabel->setToolTip(QString::number(watchingOps.size()) + QObject::tr(" transaction computing."));
        }
    });
}

void RPC::checkForUpdate(bool silent) {
    if  (conn == nullptr) 
        return noConnection();

    QUrl cmcURL("https://api.github.com/repos/Fair-Exchange/safewallet/releases");

    QNetworkRequest req;
    req.setUrl(cmcURL);
    
    QNetworkReply *reply = conn->restclient->get(req);

    QObject::connect(reply, &QNetworkReply::finished, [=] {
        reply->deleteLater();

        try {
            if (reply->error() == QNetworkReply::NoError) {

                auto releases = QJsonDocument::fromJson(reply->readAll()).array();
                QVersionNumber maxVersion(0, 0, 0);

                for (QJsonValue rel : releases) {
                    if (!rel.toObject().contains("tag_name"))
                        continue;

                    QString tag = rel.toObject()["tag_name"].toString();
                    if (tag.startsWith("v"))
                        tag = tag.right(tag.length() - 1);

                    if (!tag.isEmpty()) {
                        auto v = QVersionNumber::fromString(tag);
                        if (v > maxVersion)
                            maxVersion = v;
                    }
                }

                auto currentVersion = QVersionNumber::fromString(APP_VERSION);
                
                // Get the max version that the user has hidden updates for
                QSettings s;
                auto maxHiddenVersion = QVersionNumber::fromString(s.value("update/lastversion", "0.0.0").toString());

                qDebug() << "Version check: Current " << currentVersion << ", Available " << maxVersion;

                if (maxVersion > currentVersion && (!silent || maxVersion > maxHiddenVersion)) {
                    auto ans = QMessageBox::information(main, QObject::tr("Update Available"), 
                        QObject::tr("A new release v%1 is available! You have v%2.\n\nWould you like to visit the releases page?")
                            .arg(maxVersion.toString())
                            .arg(currentVersion.toString()),
                        QMessageBox::Yes, QMessageBox::Cancel);
                    if (ans == QMessageBox::Yes) {
                        QDesktopServices::openUrl(QUrl("https://github.com/Fair-Exchange/safewallet/releases"));
                    } else {
                        // If the user selects cancel, don't bother them again for this version
                        s.setValue("update/lastversion", maxVersion.toString());
                    }
                } else {
                    if (!silent) {
                        QMessageBox::information(main, QObject::tr("No updates available"), 
                            QObject::tr("You already have the latest release v%1")
                                .arg(currentVersion.toString()));
                    }
                } 
            }
        } catch (const std::exception& e) {
            // If anything at all goes wrong, move on
            qDebug() << QString("Exception checking for updates!");
        }
        catch (...) {
            // If anything at all goes wrong, just set the price to 0 and move on.
            qDebug() << QString("Caught something nasty");
        }       
    });
}

// Get the SAFE prices
void RPC::refreshPrice() {
    if  (conn == nullptr)
        return noConnection();


    //    QUrl cmcURL("https://api.coinpaprika.com/v1/ticker/safe-safecoin");

    // TODO: use/render all this data
    QString price_feed = "https://api.coingecko.com/api/v3/simple/price?ids=safe-coin-2&vs_currencies=btc%2Cusd%2Ceur%2Ceth%2Cgbp%2Ccny%2Cjpy%2Cidr%2Crub%2Ccad%2Csgd%2Cchf%2Cinr%2Caud%2Cinr%2Ckrw%2Cthb%2Cnzd%2Czar%2Cvef%2Cxau%2Cxag%2Cvnd%2Csar%2Ctwd%2Caed%2Cars%2Cbdt%2Cbhd%2Cbmd%2Cbrl%2Cclp%2Cczk%2Cdkk%2Chuf%2Cils%2Ckwd%2Clkr%2Cpkr%2Cnok%2Ctry%2Csek%2Cmxn%2Cuah%2Chkd&include_market_cap=true&include_24hr_vol=true&include_24hr_change=true";
    QUrl cmcURL(price_feed);
    QNetworkRequest req;
    req.setUrl(cmcURL);

    QNetworkReply *reply = conn->restclient->get(req);
    auto s = Settings::getInstance();

    qDebug() << "Requesting price feed data via " << price_feed;

    QObject::connect(reply, &QNetworkReply::finished, [=] {
        reply->deleteLater();

        try {

            QByteArray ba_raw_reply = reply->readAll();
            QString raw_reply = QString::fromUtf8(ba_raw_reply);
            QByteArray unescaped_raw_reply = raw_reply.toUtf8();
            QJsonDocument jd_reply = QJsonDocument::fromJson(unescaped_raw_reply);
            QJsonObject parsed = jd_reply.object();

            if (reply->error() != QNetworkReply::NoError) {
                qDebug() << "Parsing price feed response";

                if (!parsed.isEmpty() && !parsed["error"].toObject()["message"].isNull()) {
                    qDebug() << parsed["error"].toObject()["message"].toString();
                } else {
                    qDebug() << reply->errorString();
                }
                s->setZECPrice(0);
                s->setBTCPrice(0);
                return;
            }


            auto all = reply->readAll();


            qDebug() << "No network errors";

            if (parsed.isEmpty()) {

                s->setZECPrice(0);
                s->setBTCPrice(0);
                return;
            }

            qDebug() << "Parsed JSON";


            const QJsonValue& item  = parsed;
            const QJsonValue& safe  = item["safe-coin-2"].toObject();
            QString  ticker    = s->get_currency_name();
            ticker = ticker.toLower();
            fprintf(stderr,"ticker=%s\n", ticker.toLocal8Bit().data());
            //qDebug() << "Ticker = " + ticker;

            if (!safe[ticker].isUndefined()) {
                qDebug() << "Found safe key in price json";
                //QString price = safe["usd"].toString());
                qDebug() << "SAFE = $" << QString::number(safe["usd"].toDouble()) << " USD";
                qDebug() << "SAFE = " << QString::number(safe["eur"].toDouble()) << " EUR";
                qDebug() << "SAFE = " << QString::number((int) 100000000 * safe["btc"].toDouble()) << " sat ";

                s->setZECPrice( safe[ticker].toDouble() );
                s->setBTCPrice( (unsigned int) 100000000 * safe["btc"].toDouble() );


                ticker = ticker.toLower();
                qDebug() << "ticker=" << ticker;
                // TODO: work harder to prevent coredumps!
                auto price = safe[ticker].toDouble();
                auto vol   = safe[ticker + "_24h_vol"].toDouble();
                auto mcap  = safe[ticker + "_market_cap"].toDouble();

		//                auto btcprice = safe["btc"].toDouble();
                auto btcvol   = safe["btc_24h_vol"].toDouble();
                auto btcmcap  = safe["btc_market_cap"].toDouble();


                s->set_price(ticker, price);
                s->set_volume(ticker, vol);
                s->set_volume("BTC", btcvol);
                s->set_marketcap(ticker, mcap);

                qDebug() << "Volume = " << (double) vol;

                ticker = ticker.toUpper();
                ui->volume->setText( QString::number((double) vol, 'f', 2) + " " + ticker );
                ui->volumeBTC->setText( QString::number((double) btcvol, 'f', 2) + " BTC" );

                ticker = ticker.toUpper();
                // We don't get an actual SAFE volume stat, so we calculate it
                if (price > 0)
                    ui->volumeLocal->setText( QString::number((double) vol / (double) price) + " SAFE");

                qDebug() << "Mcap = " << (double) mcap;
                ui->marketcap->setText(  QString::number( (double) mcap, 'f', 2) + " " + ticker );
                ui->marketcapBTC->setText( QString::number((double) btcmcap, 'f', 2) + " BTC" );
                //ui->marketcapLocal->setText( QString::number((double) mcap * (double) price) + " " + ticker );


                refresh(true);
                return;
            } else {
	      QString price = parsed["price_usd"].toString();
                qDebug() << Settings::getTokenName() << " Price=" << price;
                Settings::getInstance()->setZECPrice(price.toDouble());
                return;
            }
        } catch (...) {
            // If anything at all goes wrong, just set the price to 0 and move on.
            qDebug() << QString("Price feed update failure : ");
        }

        // If nothing, then set the price to 0;
        Settings::getInstance()->setZECPrice(0);
    });
}

void RPC::shutdownZcashd() {
    // Shutdown embedded safecoind if it was started
    if (ezcashd == nullptr || ezcashd->processId() == 0 || conn == nullptr) {
        // No safecoind running internally, just return
        return;
    }

    QString method = "stop";

    conn->doRPCWithDefaultErrorHandling(makePayload(method), [=](auto) {});
    conn->shutdown();

    QDialog d(main);
    Ui_ConnectionDialog connD;
    connD.setupUi(&d);
    //connD.topIcon->setBasePixmap(QIcon(":/icons/res/icon.ico").pixmap(256, 256));
    QMovie *movie1 = new QMovie(":/img/res/safecoindlogo.gif");;
    QMovie *movie2 = new QMovie(":/img/res/safecoindlogo.gif");;
    auto theme = Settings::getInstance()->get_theme_name();
    if (theme == "dark") {
        movie2->setScaledSize(QSize(256,256));
        connD.topIcon->setMovie(movie2);
        movie2->start();
    } else {
        movie1->setScaledSize(QSize(256,256));
        connD.topIcon->setMovie(movie1);
        movie1->start();
    }


    connD.status->setText(QObject::tr("Please wait for SafeWallet to exit"));
    connD.statusDetail->setText(QObject::tr("Waiting for safecoind to exit, Stay Safe"));


    QTimer waiter(main);

    // We capture by reference all the local variables because of the d.exec() 
    // below, which blocks this function until we exit. 
    int waitCount = 0;
    QObject::connect(&waiter, &QTimer::timeout, [&] () {
        waitCount++;

        if ((ezcashd->atEnd() && ezcashd->processId() == 0) ||
            ezcashd->state() == QProcess::NotRunning ||
            waitCount > 30 ||
            conn->config->zcashDaemon)  {   // If safecoind is daemon, then we don't have to do anything else
            qDebug() << "Ended";
            waiter.stop();
            QTimer::singleShot(1000, [&]() { d.accept(); });
        } else {
            qDebug() << "Not ended, continuing to wait...";
        }
    });
    waiter.start(1000);

    // Wait for the safecoin process to exit.
    if (!Settings::getInstance()->isHeadless()) {
        d.exec(); 
    } else {
        while (waiter.isActive()) {
            QCoreApplication::processEvents();

            QThread::sleep(1);
        }
    }
}

/** 
 * Get a Sapling address from the user's wallet
 */ 
QString RPC::getDefaultSaplingAddress() {
    for (QString addr: *zaddresses) {
        if (Settings::getInstance()->isSaplingAddress(addr))
            return addr;
    }

    return QString();
}

QString RPC::getDefaultTAddress() {
    if (getAllTAddresses()->length() > 0)
        return getAllTAddresses()->at(0);
    else 
        return QString();
}
