/*
 * modlibrary.cpp
 * --------------
 * Purpose: Implementation of the Mod Library main window.
 * Notes  : (currently none)
 * Authors: Johannes Schultz
 * The Mod Library source code is released under the BSD license. Read LICENSE for more details.
 */

#include "modlibrary.h"
#include "modinfo.h"
#include "settings.h"
#include "about.h"
#include "database.h"
#include "tablemodel.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QThread>
#include <QProgressDialog>
#include <QClipboard>
#include <QSettings>
#include <utility>
#include <libopenmpt/libopenmpt.hpp>
#include <chromaprint/src/chromaprint.h>
#ifdef _MSC_VER
#include <smmintrin.h>
#endif


ModLibrary::ModLibrary(QWidget *parent)
	: QMainWindow(parent)
{
	ui.setupUi(this);

	
	QSettings settings;
	settings.beginGroup("Window");
	resize(settings.value("size", size()).toSize());
	move(settings.value("pos", pos()).toPoint());
	if(settings.value("maximized", false).toBool())
	{
		setWindowState(windowState() | Qt::WindowMaximized);
	}
	settings.endGroup();
	lastDir = settings.value("lastdir", "").toString();

	try
	{
		ModDatabase::Instance().Open();
	} catch(ModDatabase::Exception &e)
	{
		QMessageBox(QMessageBox::Critical, "Mod Library", e.what()).exec();
		QTimer::singleShot(0, this, SLOT(close()));
		return;
	}

	// Menu
	connect(ui.actionAddFile, SIGNAL(triggered()), this, SLOT(OnAddFile()));
	connect(ui.actionAddFolder, SIGNAL(triggered()), this, SLOT(OnAddFolder()));
	connect(ui.actionExportPlaylist, SIGNAL(triggered()), this, SLOT(OnExportPlaylist()));
	connect(ui.actionSettings, SIGNAL(triggered()), this, SLOT(OnSettings()));
	connect(ui.actionAbout, SIGNAL(triggered()), this, SLOT(OnAbout()));
	connect(ui.actionFindDuplicates, SIGNAL(triggered()), this, SLOT(OnFindDupes()));

	// Search navigation
	connect(ui.doSearch, SIGNAL(clicked()), this, SLOT(OnSearch()));
	connect(ui.actionShow, SIGNAL(triggered()), this, SLOT(OnShowAll()));
	connect(ui.actionMaintain, SIGNAL(triggered()), this, SLOT(OnMaintain()));
	connect(ui.findWhat, SIGNAL(returnPressed()), this, SLOT(OnSearch()));
	connect(ui.melody, SIGNAL(returnPressed()), this, SLOT(OnSearch()));
	connect(ui.fingerprint, SIGNAL(returnPressed()), this, SLOT(OnSearch()));
	connect(ui.pasteMPT, SIGNAL(clicked()), this, SLOT(OnPasteMPT()));

	connect(ui.resultTable, SIGNAL(doubleClicked(const QModelIndex &)), this, SLOT(OnCellClicked(const QModelIndex &)));

	checkBoxes.push_back(ui.findFilename);
	checkBoxes.push_back(ui.findTitle);
	checkBoxes.push_back(ui.findArtist);
	checkBoxes.push_back(ui.findSampleText);
	checkBoxes.push_back(ui.findInstrumentText);
	checkBoxes.push_back(ui.findComments);
	checkBoxes.push_back(ui.findPersonal);
	for(auto cb = checkBoxes.begin(); cb != checkBoxes.end(); cb++)
	{
		connect(*cb, SIGNAL(rightClicked(QCheckBoxEx *)), this, SLOT(OnSelectOne(QCheckBoxEx *)));
		connect(*cb, SIGNAL(middleClicked(QCheckBoxEx *)), this, SLOT(OnSelectAllButOne(QCheckBoxEx *)));
	}
}


ModLibrary::~ModLibrary()
{

}


void ModLibrary::closeEvent(QCloseEvent *event)
{
	QSettings settings;
	settings.beginGroup("Window");
	if(!isMaximized())
	{
		settings.setValue("size", size());
		settings.setValue("pos", pos());
	}
	settings.setValue("maximized", isMaximized());
	settings.endGroup();
	settings.setValue("lastdir", lastDir);

	event->accept();
}


void ModLibrary::OnAddFile()
{
	static QString modExtensions;
	if(modExtensions.isEmpty())
	{
		auto exts = openmpt::get_supported_extensions();
		for(auto ext = exts.cbegin(); ext != exts.cend(); ext++)
		{
			if(ext != exts.cbegin()) modExtensions += " ";
			modExtensions += QString("*.") + ext->c_str();
		}
	}

	QFileDialog dlg(this, tr("Select file(s) to add..."), lastDir, tr("Module files") + " (" + modExtensions + ");;" + tr("All files") + " (*.*)");
	dlg.setAcceptMode(QFileDialog::AcceptOpen);
	dlg.setFileMode(QFileDialog::ExistingFiles);
	if(dlg.exec())
	{
		auto fileNames = dlg.selectedFiles();

		QProgressDialog progress(tr("Scanning files..."), tr("Cancel"), 0, 0, this);
		progress.setWindowModality(Qt::WindowModal);
		progress.setRange(0, fileNames.size());
		progress.setValue(0);
		progress.show();

		uint files = 0;
		for(auto file = fileNames.cbegin(); file != fileNames.cend() && !progress.wasCanceled(); file++)
		{
			if(file == fileNames.cbegin()) lastDir =  QFileInfo(*file).absoluteDir().absolutePath();
			ModDatabase::Instance().AddModule(*file);
			progress.setValue(++files);
			QCoreApplication::processEvents();
		}
	}
}


void ModLibrary::OnAddFolder()
{
	QFileDialog dlg(this, tr("Select folder to add..."), lastDir);
	dlg.setFileMode(QFileDialog::DirectoryOnly);
	if(dlg.exec())
	{
		const QString path = dlg.selectedFiles().first();
		lastDir = path;

		QDirIterator di(path, QDir::Files, QDirIterator::Subdirectories);

		QProgressDialog progress(tr("Scanning files..."), tr("Cancel"), 0, 0, this);
		progress.setWindowModality(Qt::WindowModal);
		progress.setRange(0, 0);
		progress.setValue(0);
		progress.show();

		// TODO: Possibly make this multi-threaded, and allow the users to filter out file types (e.g. .bak)
		uint addedFiles = 0, updatedFiles = 0;
		while(di.hasNext() && !progress.wasCanceled())
		{
			const QString fileName = di.next();
			progress.setLabelText(tr("Analyzing %1...\n%2 files added, %3 files updated.").arg(QDir::toNativeSeparators(fileName)).arg(addedFiles).arg(updatedFiles));
			QCoreApplication::processEvents();
			switch(ModDatabase::Instance().AddModule(fileName))
			{
			case ModDatabase::Added:
				addedFiles++;
				break;
			case ModDatabase::Updated:
				updatedFiles++;
				break;
			}
		}
	}
}


void ModLibrary::OnMaintain()
{
	QSqlQuery query(ModDatabase::Instance().GetDB());
	query.exec("SELECT COUNT(*) FROM `modlib_modules`");
	query.next();
	uint numFiles = query.value(0).toUInt();

	query.exec("SELECT `filename` FROM `modlib_modules`");

	QProgressDialog progress("Scanning files...", "Cancel", 0, 0, this);
	progress.setWindowModality(Qt::WindowModal);
	progress.setRange(0, numFiles);
	progress.setValue(0);
	progress.show();

	uint files = 0, updatedFiles = 0, removedFiles = 0;
	while(query.next() && !progress.wasCanceled())
	{
		const QString fileName = query.value(0).toString();
		progress.setLabelText(tr("Analyzing %1...\n%2 files updated, %3 files removed.").arg(QDir::toNativeSeparators(fileName)).arg(updatedFiles).arg(removedFiles));
		QCoreApplication::processEvents();
		switch(ModDatabase::Instance().UpdateModule(fileName))
		{
		case ModDatabase::Updated:
		case ModDatabase::Added:
			updatedFiles++;
			break;
		case ModDatabase::NoChange:
			break;
		case ModDatabase::IOError:
		case ModDatabase::NotAdded:
			removedFiles++;
			ModDatabase::Instance().RemoveModule(fileName);
			break;
		}
		progress.setValue(++files);
	}
}


void ModLibrary::DoSearch(bool showAll)
{
	setCursor(Qt::BusyCursor);

	QString what = ui.findWhat->text();
	what.replace('\\', "\\\\")
		.replace('%', "\\%")
		.replace('_', "\\_")
		.replace('*', "%")
		.replace('?', "_");
	what = "%" + what + "%";

	std::vector<QByteArray> melodyBytes;
	QByteArray fingerprint = ui.fingerprint->text().trimmed().toLatin1();
	uint32_t *rawFingerprint = nullptr;
	int rawFingerprintSize = 0;
	chromaprint_decode_fingerprint(fingerprint.data(), fingerprint.size(), (void **)&rawFingerprint, &rawFingerprintSize, nullptr, 1);

	QString queryStr = "SELECT `filename`, `title`, `filesize`, `filedate` ";
	if(rawFingerprintSize)
	{
		queryStr += ", `fingerprint` ";

	}
	queryStr += "FROM `modlib_modules` ";
	if(!showAll)
	{
		queryStr += "WHERE (0 ";
		if(ui.findFilename->isChecked())		queryStr += "OR `filename` LIKE :str ESCAPE '\\' ";
		if(ui.findTitle->isChecked())			queryStr += "OR `title` LIKE :str ESCAPE '\\' ";
		if(ui.findArtist->isChecked())			queryStr += "OR `artist` LIKE :str ESCAPE '\\' ";
		if(ui.findSampleText->isChecked())		queryStr += "OR `sample_text` LIKE :str ESCAPE '\\' ";
		if(ui.findInstrumentText->isChecked())	queryStr += "OR `instrument_text` LIKE :str ESCAPE '\\' ";
		if(ui.findComments->isChecked())		queryStr += "OR `comments` LIKE :str ESCAPE '\\' ";
		if(ui.findPersonal->isChecked())		queryStr += "OR `personal_comments` LIKE :str ESCAPE '\\' ";
		queryStr += ") ";

		if(ui.limitSize->isChecked())
		{
			const auto factor = 1 << (10 * ui.limitSizeUnit->currentIndex());
			auto sizeMin = ui.limitMinSize->value() * factor, sizeMax = ui.limitMaxSize->value() * factor;
			if(sizeMin > sizeMax) std::swap(sizeMin, sizeMax);
			queryStr += "AND (`filesize` BETWEEN " + QString::number(sizeMin) + " AND " + QString::number(sizeMax) + ") ";
		}
		if(ui.limitFileDate->isChecked())
		{
			auto dateMin = QDateTime(ui.limitFileDateMin->date()).toTime_t();
			auto dateMax = QDateTime(ui.limitFileDateMax->date(), QTime(23, 59, 59)).toTime_t();
			if(dateMin > dateMax) std::swap(dateMin, dateMax);
			queryStr += "AND (`filedate` BETWEEN " + QString::number(dateMin) + " AND " + QString::number(dateMax) + ") ";
		}
		if(ui.limitYear->isChecked())
		{
			auto dateMin = QDateTime(ui.limitReleaseDateMin->date(), QTime(0, 0, 0)).toTime_t();
			auto dateMax = QDateTime(ui.limitReleaseDateMax->date(), QTime(23, 59, 59)).toTime_t();
			if(dateMin > dateMax) std::swap(dateMin, dateMax);
			queryStr += "AND (`editdate` BETWEEN " + QString::number(dateMin) + " AND " + QString::number(dateMax) + ") ";
		}
		if(ui.limitTime->isChecked())
		{
			auto timeMin = ui.limitTimeMin->value() * 1000, timeMax = ui.limitTimeMax->value() * 1000;
			if(timeMin > timeMax) std::swap(timeMin, timeMax);
			queryStr += "AND (`length` BETWEEN " + QString::number(timeMin) + " AND " + QString::number(timeMax) + ") ";
		}

		// Search for melody
		const auto melodies = ui.melody->text().split('|');
		int melodyCount = 0;
		for(auto melody = melodies.cbegin(); melody != melodies.cend(); melody++)
		{
			const auto melodyStr = melody->simplified();
			const auto notes = melodyStr.split(' ');
			if(!melodyStr.isEmpty() && !notes.isEmpty())
			{
				melodyBytes.push_back(QByteArray());
				melodyBytes[melodyCount].reserve(notes.size());
				for(auto note = notes.cbegin(); note != notes.cend(); note++)
				{
					int8_t n = static_cast<int8_t>(note->toInt());
					melodyBytes[melodyCount].push_back(n);
				}
				queryStr += "AND INSTR(`note_data`, :note_data" + QString::number(melodyCount) + ") > 0 ";
				melodyCount++;
			}
		}
	}

	QSqlQuery query(ModDatabase::Instance().GetDB());
	query.prepare(queryStr);
	query.bindValue(":str", what);
	for(size_t i = 0; i < melodyBytes.size(); i++)
	{
		query.bindValue(":note_data" + QString::number(i), melodyBytes[i]);
	}

	TableModel *model = new TableModel(query, rawFingerprint, rawFingerprintSize);
	ui.resultTable->setModel(model);

	QHeaderView *verticalHeader = ui.resultTable->verticalHeader();
	verticalHeader->setSectionResizeMode(QHeaderView::Fixed);

	QHeaderView *horizontalHeader = ui.resultTable->horizontalHeader();
	horizontalHeader->setStretchLastSection(false);
	horizontalHeader->setSectionResizeMode(0, QHeaderView::Stretch);
	for(int i = model->columnCount() - 1; i >= 1; i--)
	{
		horizontalHeader->setSectionResizeMode(i, QHeaderView::ResizeToContents);
	}

	const int numRows = model->rowCount();
	ui.statusBar->showMessage(tr("%1 files found.").arg(numRows));

	if(rawFingerprintSize)
	{
		// Sort by match quality when searching for fingerprints
		ui.resultTable->sortByColumn(3, Qt::DescendingOrder);
	}

	unsetCursor();

	if(numRows == 1 && !showAll)
	{
		// Show the only result
		OnCellClicked(model->index(0, 0));
	}
}


void ModLibrary::OnFindDupes()
{
	setCursor(Qt::BusyCursor);

	QSqlQuery query(ModDatabase::Instance().GetDB());
	query.prepare(
//		"SELECT `filename`, `title`, `filesize`, `filedate` FROM `modlib_modules` AS `m1` WHERE `filename` IN "
//		"(SELECT `filename` FROM `modlib_modules` AS `m2` WHERE `m1`.`hash` = `m2`.`hash` AND `m1`.`filename` <> `m2`.`filename`) ORDER BY `hash`"
		"SELECT `filename`, `title`, `filesize`, `filedate`, COUNT(*) FROM `modlib_modules`"
		"GROUP BY `hash` HAVING COUNT(*) > 1"
		);

	TableModel *model = new TableModel(query, nullptr, 0);
	ui.resultTable->setModel(model);

	QHeaderView *verticalHeader = ui.resultTable->verticalHeader();
	verticalHeader->setSectionResizeMode(QHeaderView::Fixed);

	QHeaderView *horizontalHeader = ui.resultTable->horizontalHeader();
	horizontalHeader->setStretchLastSection(false);
	horizontalHeader->setSectionResizeMode(0, QHeaderView::Stretch);
	for(int i = model->columnCount() - 1; i >= 1; i--)
	{
		horizontalHeader->setSectionResizeMode(i, QHeaderView::ResizeToContents);
	}

	const int numRows = model->rowCount();
	ui.statusBar->showMessage(tr("%1 files found.").arg(numRows));

	unsetCursor();
}


void ModLibrary::OnSelectOne(QCheckBoxEx *sender)
{
	sender->setChecked(true);
	for(auto cb = checkBoxes.begin(); cb != checkBoxes.end(); cb++)
	{
		if(sender != *cb)
		{
			(**cb).setChecked(false);
		}
	}
}


void ModLibrary::OnSelectAllButOne(QCheckBoxEx *sender)
{
	sender->setChecked(false);
	for(auto cb = checkBoxes.begin(); cb != checkBoxes.end(); cb++)
	{
		if(sender != *cb)
		{
			(**cb).setChecked(true);
		}
	}
}


void ModLibrary::OnCellClicked(const QModelIndex &index)
{
	const QString fileName = ui.resultTable->model()->data(index, Qt::UserRole).toString();
	ModInfo *dlg = new ModInfo(fileName, this);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->show();
}


void ModLibrary::OnExportPlaylist()
{
	if(!ui.resultTable->model()->rowCount())
	{
		OnShowAll();
	}

	const auto numRows = ui.resultTable->model()->rowCount();
	if(!numRows)
	{
		QMessageBox mb(QMessageBox::Information, tr("Your library is empty."), tr("Mod Library"));
		mb.exec();
		return;
	}

	QFileDialog dlg(this, tr("Save Playlist..."), lastDir, tr("Playlist files (*.pls)"));
	dlg.setAcceptMode(QFileDialog::AcceptSave);
	if(dlg.exec())
	{
		auto files = dlg.selectedFiles();
		if(!files.isEmpty())
		{
			QFile file(files.first());
			if(file.open(QIODevice::WriteOnly | QIODevice::Text))
			{
				setCursor(Qt::BusyCursor);
				QTextStream fout(&file);
				fout << "[playlist]" << endl;
				fout << "numberofentries=" << numRows << endl;
				for(auto row = 0; row < numRows; row++)
				{
					fout << "file" << (row + 1) << "=" << QDir::toNativeSeparators(ui.resultTable->model()->data(ui.resultTable->model()->index(row, 0), Qt::UserRole).toString()) << endl;
					fout << "title" << (row + 1) << "=" << ui.resultTable->model()->data(ui.resultTable->model()->index(row, 0), Qt::DisplayRole).toString() << endl;
				}
				unsetCursor();
			}
		}
	}
}


// Interpret pasted OpenMPT pattern format for melody search
void ModLibrary::OnPasteMPT()
{
	const QString notes[] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
	const QClipboard *clipboard = QApplication::clipboard();
	const QMimeData *mimeData = clipboard->mimeData();

	if(mimeData->hasText())
	{
		QString melody;
		QString data = mimeData->text();
		auto offset = data.indexOf("ModPlug Tracker ");
		if(offset == -1)
		{
			return;
		}
		data.remove(0, offset + 16);
		auto lines = data.split('\n');
		int channels = 0;
		int prevNote;
		do
		{
			prevNote = -1;
			for(auto line = lines.cbegin(); line != lines.cend(); line++)
			{
				int offset = -1;
				for(int i = 0; i <= channels; i++)
				{
					offset = line->indexOf("|", offset + 1);
					if(offset == -1)
					{
						break;
					}
				}
				if(offset == -1)
				{
					continue;
				}

				// This appears to be a valid channel
				if(prevNote == -1) prevNote = 0;

				int note = 0;
				char octave = line->at(offset + 3).toLatin1();
				if(octave >= '0' && octave <= '9')
				{
					const auto noteStr = line->midRef(offset + 1, 2);
					for(auto i = 0; i < 12; i++)
					{
						if(notes[i] == noteStr)
						{
							note = i + (octave - '0') * 12;
							break;
						}
					}
				}
				if(note)
				{
					int diff = note - prevNote;
					if(prevNote)
					{
						melody += QString::number(diff) + " ";
					}
					prevNote = note;
				}
			}
			channels++;
			if(prevNote)
			{
				melody += "|";
			}
		} while (prevNote != -1);


		ui.melody->setText(melody);
	}
}


void ModLibrary::OnSettings()
{
	SettingsDialog dlg(this);
	dlg.exec();
}


void ModLibrary::OnAbout()
{
	AboutDialog dlg(this);
	dlg.exec();
}
