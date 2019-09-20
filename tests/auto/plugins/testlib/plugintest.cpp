#include "plugintest.h"
#include <QtCore/private/qfactoryloader_p.h>
#include <QtAutoUpdaterCore/private/updater_p.h>
using namespace QtAutoUpdater;
using namespace std::chrono;
using namespace std::chrono_literals;

Q_GLOBAL_STATIC_WITH_ARGS(QFactoryLoader, loader,
						  (QtAutoUpdater_UpdaterPlugin_iid,
						   QLatin1String("/updaters")))

bool PluginTest::init()
{
	return true;
}

bool PluginTest::cleanup()
{
	return true;
}

QVariantMap PluginTest::performConfig()
{
	qDebug() << Q_FUNC_INFO;
	return config();
}

void PluginTest::initTestCase()
{
	QVERIFY(init());
}

void PluginTest::cleanupTestCase()
{
	QVERIFY(cleanup());
}

void PluginTest::testUpdateCheck_data()
{
	QTest::addColumn<QVersionNumber>("installedVersion");
	QTest::addColumn<QVersionNumber>("updateVersion");
	QTest::addColumn<int>("abortLevel");
	QTest::addColumn<bool>("success");

	QTest::newRow("noUpdates") << QVersionNumber(1, 0, 0)
							   << QVersionNumber(1, 0, 0)
							   << 0
							   << true;

	QTest::newRow("simpleUpdate") << QVersionNumber(1, 0, 0)
								  << QVersionNumber(1, 1, 0)
								  << 0
								  << true;

	QTest::newRow("newerInstall") << QVersionNumber(1, 1, 0)
								  << QVersionNumber(1, 0, 0)
								  << 0
								  << true;

	QTest::newRow("abortSoft") << QVersionNumber(1, 0, 0)
							   << QVersionNumber(1, 1, 0)
							   << 1
							   << cancelState();

	QTest::newRow("abortHard") << QVersionNumber(1, 0, 0)
							   << QVersionNumber(1, 1, 0)
							   << 2
							   << cancelState();
}

void PluginTest::testUpdateCheck()
{
	qDebug() << Q_FUNC_INFO << "enter";
	QFETCH(QVersionNumber, installedVersion);
	QFETCH(QVersionNumber, updateVersion);
	QFETCH(int, abortLevel);
	QFETCH(bool, success);

	if (abortLevel > 0) {
		if (!canAbort(abortLevel == 2)) {
			QEXPECT_FAIL("", "Backend does not support given abort level", Abort);
			QVERIFY(false);
		}
	}

	qDebug() << Q_FUNC_INFO << "before install";
	QVERIFY(simulateInstall(installedVersion));
	QVERIFY(prepareUpdate(updateVersion));
	const auto updates = abortLevel > 0 ?
							 QList<UpdateInfo>{} :
							 createInfos(installedVersion, updateVersion);
	qDebug() << Q_FUNC_INFO << "after install";

	sptr updater { loadBackend() };
	if (!updater)
		return;
	QSignalSpy doneSpy{updater.data(), &UpdaterBackend::checkDone};
	QVERIFY(doneSpy.isValid());

	//start the check updates
	qDebug() << Q_FUNC_INFO << "before check";
	updater->checkForUpdates();

	if (abortLevel > 0) {
		if (!doneSpy.wait(0)) {
			updater->abort(abortLevel == 2);
			QVERIFY(doneSpy.wait(60000));
		}
	} else  //wait max 1 min for the process to finish
		QVERIFY(doneSpy.wait(60000));
	qDebug() << Q_FUNC_INFO << "after check";

	//check if the finished signal is without error
	QCOMPARE(doneSpy.size(), 1);
	QCOMPARE(doneSpy[0][0].toBool(), success);
	QCOMPARE(doneSpy[0][1].value<QList<UpdateInfo>>(), updates);
	qDebug() << Q_FUNC_INFO << "exit";
}

void PluginTest::testTriggerInstall()
{
	QVERIFY(simulateInstall(QVersionNumber{1,0,0}));
	QVERIFY(prepareUpdate(QVersionNumber{1,1,0}));

	sptr updater { loadBackend() };
	QVERIFY(updater);

	if (!updater->features().testFlag(UpdaterBackend::Feature::ParallelTrigger)) {
		QEXPECT_FAIL("", "Backend does not support parallel triggered installations", Abort);
		QVERIFY(false);
	}

	// check for updates
	QSignalSpy doneSpy{updater.data(), &UpdaterBackend::checkDone};
	QVERIFY(doneSpy.isValid());
	updater->checkForUpdates();
	QVERIFY(doneSpy.wait(60000));
	QCOMPARE(doneSpy.size(), 1);
	QCOMPARE(doneSpy[0][0].toBool(), true);
	const auto updates = doneSpy[0][1].value<QList<UpdateInfo>>();
	QVERIFY(updates.size() > 0);

	// install the updates
	QSignalSpy installSpy{updater.data(), &UpdaterBackend::triggerInstallDone};
	QVERIFY(installSpy.isValid());
	QVERIFY(updater->triggerUpdates(updates, true));
	QVERIFY(installSpy.wait(60000));
	QCOMPARE(installSpy.size(), 1);
	QCOMPARE(installSpy.takeFirst()[0].toBool(), true);

	// check for updates again to make shure there are none
	doneSpy.clear();
	updater->checkForUpdates();
	QVERIFY(doneSpy.wait(60000));
	QCOMPARE(doneSpy.size(), 1);
	QCOMPARE(doneSpy[0][0].toBool(), true);
	QVERIFY(doneSpy[0][1].value<QList<UpdateInfo>>().isEmpty());
}

void PluginTest::testPerformInstall()
{
	QVERIFY(simulateInstall(QVersionNumber{1,1,0}));
	QVERIFY(prepareUpdate(QVersionNumber{1,2,0}));

	sptr updater { loadBackend(true) };
	QVERIFY(updater);

	if (!updater->features().testFlag(UpdaterBackend::Feature::PerformInstall)) {
		QEXPECT_FAIL("", "Backend does not support performed installations", Abort);
		QVERIFY(false);
	}

	// check for updates
	QSignalSpy doneSpy{updater.data(), &UpdaterBackend::checkDone};
	QVERIFY(doneSpy.isValid());
	updater->checkForUpdates();
	QVERIFY(doneSpy.wait(60000));
	QCOMPARE(doneSpy.size(), 1);
	QCOMPARE(doneSpy[0][0].toBool(), true);
	const auto updates = doneSpy[0][1].value<QList<UpdateInfo>>();
	QVERIFY(updates.size() > 0);

	// prepare the installer
	auto installer = updater->createInstaller();
	QVERIFY(installer);
	QSignalSpy successSpy{installer, &UpdateInstaller::installSucceeded};
	QVERIFY(successSpy.isValid());
	QSignalSpy failSpy{installer, &UpdateInstaller::installFailed};
	QVERIFY(failSpy.isValid());
	connect(installer, &UpdateInstaller::showEula,
			installer, [installer](const QVariant &id, const QString &, bool required) {
		if (required)
			installer->eulaHandled(id, true);
	});

	// install the updates
	installer->setComponents(updates);
	installer->startInstall();
	QCOMPARE(failSpy.size(), 0);
	if (successSpy.isEmpty())
		QVERIFY(successSpy.wait(60000));
	QCOMPARE(successSpy.size(), 1);
	QCOMPARE(failSpy.size(), 0);
	installer->deleteLater();

	// check for updates again to make shure there are none
	doneSpy.clear();
	updater->checkForUpdates();
	QVERIFY(doneSpy.wait(60000));
	QCOMPARE(doneSpy.size(), 1);
	QCOMPARE(doneSpy[0][0].toBool(), true);
	QVERIFY(doneSpy[0][1].value<QList<UpdateInfo>>().isEmpty());
}

UpdaterBackend *PluginTest::loadBackend(bool asPerform)
{
	qDebug() << Q_FUNC_INFO << "enter" << asPerform;
	UpdaterBackend *backendRes = nullptr;
	[&]() {
		QVERIFY(Updater::supportedUpdaterBackends().contains(backend()));
		auto uBackend = qLoadPlugin<UpdaterBackend, UpdaterPlugin>(loader, backend(), this);
		QVERIFY(uBackend);
		auto reader = new VariantConfigReader{backend(), asPerform ? performConfig() : config()};
		QVERIFY(uBackend->initialize(QScopedPointer<UpdaterBackend::IConfigReader>{reader}));
		backendRes = uBackend;
	}();
	qDebug() << Q_FUNC_INFO << "exit";
	return backendRes;
}
