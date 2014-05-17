/*
 * database.cpp
 * ------------
 * Purpose: Implementation of the Mod Library database functionality.
 * Notes  : (currently none)
 * Authors: Johannes Schultz
 * The Mod Library source code is released under the BSD license. Read LICENSE for more details.
 */

#include "database.h"
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDebug>
#include <libopenmpt/libopenmpt.hpp>

#define SCHEMA_VERSION 1
#define VER_HELPER_STRINGIZE(x) #x
#define VER_STRINGIZE(x)        VER_HELPER_STRINGIZE(x)
#define SCHEMA_VERSION_STR VER_STRINGIZE(SCHEMA_VERSION)


ModDatabase ModDatabase::instance;

void ModDatabase::Open()
{
	db = QSqlDatabase::addDatabase("QSQLITE");
	QString dbFile = QStandardPaths::writableLocation(QStandardPaths::DataLocation) + QDir::separator();
	QDir().mkpath(dbFile);
	dbFile += "Mod Library.sqlite";
	const QString dbBackup = dbFile + "~";
	QFile::remove(dbBackup);
	QFile::copy(dbFile, dbBackup);
	db.setDatabaseName(dbFile);

	if(!db.open())
	{
		throw Exception("Cannot option database: ", db.lastError());
	}
	QSqlQuery query(db);
	if(!query.exec("CREATE TABLE IF NOT EXISTS `modlib_schema` (`name` TEXT PRIMARY KEY, `value` TEXT)"))
	{
		throw Exception("Cannot create schema table: ", query.lastError());
	}

	if(!query.exec("SELECT `value` FROM `modlib_schema` WHERE `name` = 'schema_version'"))
	{
		throw Exception("Cannot retrieve schema information: ", query.lastError());
	}
	int schemaVersion = 0;
	if(query.next())
	{
		schemaVersion = query.value(0).toInt();
	}

	if(schemaVersion == 0)
	{
		if(!query.exec("CREATE TABLE IF NOT EXISTS `modlib_modules` ("
			"`hash` TEXT PRIMARY KEY, "
			"`filename` TEXT UNIQUE, "
			"`filesize` INT, "
			"`filedate` INT, "
			"`editdate` INT, "
			"`format` TEXT, "
			"`title` TEXT, "
			"`length` INT, "
			"`num_channels` INT, "
			"`num_patterns` INT, "
			"`num_orders` INT, "
			"`num_subsongs` INT, "
			"`num_samples` INT, "
			"`num_instruments` INT, "
			"`sample_text` TEXT, "
			"`instrument_text` TEXT, "
			"`comments` TEXT, "
			"`artist` TEXT, "
			"`personal_comments` TEXT, "
			"`note_data` BLOB COLLATE BINARY)"))
		{
			throw Exception("Cannot update library schema: ", query.lastError());
		}

		if(!query.exec("CREATE INDEX IF NOT EXISTS `modlib_title` ON `modlib_modules` (`title`)")
			|| !query.exec("CREATE INDEX IF NOT EXISTS `modlib_filename` ON `modlib_modules` (`filename`)"))
		{
			throw Exception("Cannot create library indices: ", query.lastError());
		}

		if(!query.exec("INSERT OR IGNORE INTO `modlib_schema` (`name`, `value`) VALUES ('schema_version', '" SCHEMA_VERSION_STR "')")
			|| !query.exec("UPDATE `modlib_schema` SET `value` = '" SCHEMA_VERSION_STR "' WHERE `name` = 'schema_version'"))
		{
			throw Exception("Cannot update schema table: ", query.lastError());
		}
	}

	insertQuery = QSqlQuery(db);
	if(!insertQuery.prepare("INSERT INTO `modlib_modules` ("
		"`hash`, `filename`, `filesize`, `filedate`, `editdate`, `format`, `title`, `length`, `num_channels`, `num_patterns`, `num_orders`, `num_subsongs`, `num_samples`, `num_instruments`, `sample_text`, `instrument_text`, `comments`, `artist`, `note_data`) "
		" VALUES (:hash, :filename, :filesize, :filedate, :editdate, :format, :title, :length, :num_channels, :num_patterns, :num_orders, :num_subsongs, :num_samples, :num_instruments, :sample_text, :instrument_text, :comments, :artist, :note_data)"))
	{
		throw Exception("Cannot prepare insert query: ", insertQuery.lastError());
	}

	updateQuery = QSqlQuery(db);
	if(!updateQuery.prepare("UPDATE `modlib_modules` SET"
		"`hash` = :hash, `filename` = :filename, `filesize` = :filesize, `filedate` = :filedate, `editdate` = :editdate, `format` = :format, `title` = :title, `length` = :length, "
		"`num_channels` = :num_channels, `num_patterns` = :num_patterns, `num_orders` = :num_orders, `num_subsongs` = :num_subsongs, `num_samples` = :num_samples, "
		"`num_instruments` = :num_instruments, `sample_text` = :sample_text, `instrument_text` = :instrument_text, `comments` = :comments, `artist` = :artist, `note_data` = :note_data"
		" WHERE `filename` = :filename_old"))
	{
		throw Exception("Cannot prepare update query: ", updateQuery.lastError());
	}

	updateCommentsQuery = QSqlQuery(db);
	if(!updateCommentsQuery.prepare("UPDATE `modlib_modules` SET"
		"`personal_comments` = :personal_comments"
		" WHERE `filename` = :filename"))
	{
		throw Exception("Cannot prepare update comments query: ", updateCommentsQuery.lastError());
	}

	selectQuery = QSqlQuery(db);
	if(!selectQuery.prepare("SELECT * FROM `modlib_modules` WHERE `filename` = :filename"))
	{
		throw Exception("Cannot prepare select query: ", selectQuery.lastError());
	}

	removeQuery = QSqlQuery(db);
	if(!removeQuery.prepare("DELETE FROM `modlib_modules` WHERE `filename` = :filename"))
	{
		throw Exception("Cannot prepare delete query: ", selectQuery.lastError());
	}
}


ModDatabase::~ModDatabase()
{
	QSqlQuery query(db);
	query.exec("VACUUM `modlib_modules`");
	db.close();
}


ModDatabase::AddResult ModDatabase::AddModule(const QString &path)
{
	AddResult result = PrepareQuery(path, insertQuery);
	if(result != NotAdded)
		return result;
	return UpdateModule(path);
}


ModDatabase::AddResult ModDatabase::UpdateModule(const QString &path)
{
	updateQuery.bindValue(":filename_old", path);
	AddResult result = PrepareQuery(path, updateQuery);
	return result == Added ? Updated : result;
}


bool ModDatabase::UpdateComments(const QString &path, const QString &comments)
{
	updateCommentsQuery.bindValue(":filename", path);
	updateCommentsQuery.bindValue(":personal_comments", comments);
	return updateCommentsQuery.exec();
}


// Extract the notes from some module's patterns, as a byte sequence of note deltas.
static void BuildNoteString(openmpt::module &mod, QByteArray &notes)
{
	const auto numChannels = mod.get_num_channels();
	const auto numSongs = mod.get_num_subsongs();
	int8_t lastNote = 0;
	for(auto s = 0; s < numSongs; s++)
	{
		mod.select_subsong(s);
		const auto numOrders = mod.get_num_orders();
		notes.reserve(notes.size() + numChannels * numOrders * 64);
		for(auto c = 0; c < numChannels; c++)
		{
			// Go through the complete module channel by channel.
			notes.push_back(-lastNote);
			for(auto o = 0; o < numOrders; o++)
			{
				const auto p = mod.get_order_pattern(o);
				const auto numRows = mod.get_pattern_num_rows(p);
				for(auto r = 0; r < numRows; r++)
				{
					const uint8_t note = mod.get_pattern_row_channel_command(p, r, c, openmpt::module::command_note);
					if(note > 0 && note <= 128)
					{
						notes.push_back(static_cast<int8_t>(note) - lastNote);
						lastNote = note;
					}
				}
			}
		}
	}
}


ModDatabase::AddResult ModDatabase::PrepareQuery(const QString &path, QSqlQuery &query)
{
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
	{
		return IOError;
	}
	QByteArray content(file.readAll());

	try
	{
		openmpt::module mod(content.cbegin(), content.cend());
		const QByteArray hash = QCryptographicHash::hash(content, QCryptographicHash::Sha512);
		const QString hashStr = hash.toBase64();

		const QString dbPath = QDir::fromNativeSeparators(path);
		// Check if this file already exists as-is in the database.
		selectQuery.bindValue(":filename", dbPath);
		if(selectQuery.exec() && selectQuery.next())
		{
			if(selectQuery.value("hash").toString() == hashStr)
			{
				return NoChange;
			}
		}

		query.bindValue(":hash", hashStr);
		query.bindValue(":filename", dbPath);
		query.bindValue(":filesize", content.size());
		query.bindValue(":filedate", QFileInfo(file).lastModified().toTime_t());
		query.bindValue(":editdate", QDateTime::fromString(QString::fromStdString(mod.get_metadata("date")), Qt::ISODate).toTime_t());
		query.bindValue(":format", QString::fromStdString(mod.get_metadata("type")));
		query.bindValue(":title", QString::fromStdString(mod.get_metadata("title")));
		query.bindValue(":length", static_cast<int>(mod.get_duration_seconds() * 1000));
		query.bindValue(":num_channels", mod.get_num_channels());
		query.bindValue(":num_patterns", mod.get_num_patterns());
		query.bindValue(":num_orders", mod.get_num_orders());
		query.bindValue(":num_subsongs", mod.get_num_subsongs());
		query.bindValue(":num_samples", mod.get_num_samples());
		query.bindValue(":num_instruments", mod.get_num_instruments());
		{
			QString sampleText;
			auto names = mod.get_sample_names();
			for(auto name = names.cbegin(); name != names.cend(); name++)
			{
				sampleText += QString::fromStdString(*name) + "\n";
			}
			query.bindValue(":sample_text", sampleText);
		}
		{
			QString instrText;
			auto names = mod.get_instrument_names();
			for(auto name = names.cbegin(); name != names.cend(); name++)
			{
				instrText += QString::fromStdString(*name) + "\n";
			}
			query.bindValue(":instrument_text", instrText);
		}
		query.bindValue(":comments", QString::fromStdString(mod.get_metadata("message")));
		query.bindValue(":artist", QString::fromStdString(mod.get_metadata("artist")));

		QByteArray notes;
		BuildNoteString(mod, notes);
		query.bindValue(":note_data", notes);

		if(!query.exec())
		{
			qDebug() << query.lastError();
			return NotAdded;
		}
	} catch(openmpt::exception &)
	{
		return NotAdded;
	}

	return Added;
}


Module ModDatabase::GetModule(const QString &path)
{
	selectQuery.bindValue(":filename", QDir::fromNativeSeparators(path));
	selectQuery.exec();
	selectQuery.next();
	return GetModule(selectQuery);
}


Module ModDatabase::GetModule(QSqlQuery &query)
{
	Module mod;
	mod.hash = query.value("hash").toString();
	mod.fileName = query.value("filename").toString();
	mod.fileSize = query.value("filesize").toInt();
	mod.fileDate = QDateTime::fromTime_t(query.value("filedate").toInt());
	mod.editDate = QDateTime::fromTime_t(query.value("editdate").toInt());
	mod.format = query.value("format").toString();
	mod.title = query.value("title").toString();
	mod.length = query.value("length").toInt();
	mod.numChannels = query.value("num_channels").toInt();
	mod.numPatterns = query.value("num_patterns").toInt();
	mod.numOrders = query.value("num_orders").toInt();
	mod.numSubSongs = query.value("num_subsongs").toInt();
	mod.numSamples = query.value("num_samples").toInt();
	mod.numInstruments = query.value("num_instruments").toInt();
	mod.sampleText = query.value("sample_text").toString();
	mod.instrumentText = query.value("instrument_text").toString();
	mod.comments = query.value("comments").toString();
	mod.artist = query.value("artist").toString();
	mod.personalComment = query.value("personal_comments").toString();
	return mod;
}


bool ModDatabase::RemoveModule(const QString &path)
{
	removeQuery.bindValue(":filename", QDir::fromNativeSeparators(path));
	return removeQuery.exec();
}