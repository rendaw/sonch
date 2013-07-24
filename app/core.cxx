#include "core.h"

#define SplitDir "splits"
	
bool ShareFile::IsFile(void) const; // File (true) or directory (false)
bool ShareFile::OwnerRead(void) const;
bool ShareFile::OwnerWrite(void) const;
bool ShareFile::OwnerExecute(void) const;
bool ShareFile::GroupRead(void) const;
bool ShareFile::GroupWrite(void) const;
bool ShareFile::GroupExecute(void) const;
bool ShareFile::OtherRead(void) const;
bool ShareFile::OtherWrite(void) const;
bool ShareFile::OtherExecute(void) const;

ShareCore::ShareCore(bfs::path const &Root, std::string const &InstanceName = std::string()) :
	Root(Root), InstanceName(InstanceName)
{
	auto const ValidateFilename = [](std::string const &Filename)
	{
		if (Filename.empty()) return false;
		for (char const FilenameChar : Filename)
		{
			switch (FilenameChar)
			{
				case '\0':
				case '/':
#ifndef CUSTOM_STRANGE_PATHS
				case '\\': case ':': case '*': case '?': case '"': case '<': case '>': case '|':
#endif
					return false;
				default: break;
			}
		}
		return true;
	};
	auto const GetInstanceFilename = [](std::string const &Name, UUID ID)
	{
		std::vector<char> Buffer;
		Buffer.resize(Name.size() + sizeof(ID) * 2 + 1);
		memcpy(&Buffer[0], Name.c_str(), Name.size());
		Buffer[Name.size()] = '-';
		for (unsigned int Offset = 0; Offset < sizeof(ID); ++Offset)
		{
			Buffer[Name.size() + 1 + Offset * 2] = 'a' + (*(reinterpret_cast<char *>(&ID) + Offset) & 0xF);
			Buffer[Name.size() + 1 + Offset * 2 + 1] = 'a' + ((*(reinterpret_cast<char *>(&ID) + Offset) & 0xF0) >> 4);
		}
		return std::string(Buffer.begin(), Buffer.end());
	};
	auto const InitializeDatabase = [](void)
	{
		Database.reset(new SQLDatabase(DatabasePath));
		Statement.Begin = Database->Prepare<std::tuple<>>("BEGIN");
		Statement.End = Database->Prepare<std::tuple<>>("COMMIT");
		Statement.IncrementFileCounter = Database->Prepare<std::tuple<>>("UPDATE \"Counters\" SET \"File\" = \"File\" + 1");
		Statement.GetFileCounter = Database->Prepare<std::tuple<>, std::tuple<uint64_t>>("SELECT \"File\" FROM \"Counters\"");
		Statement.IncrementFileCounter = Database->Prepare<std::tuple<>>("UPDATE \"Counters\" SET \"File\" = \"File\" + 1");
		Statement.NewFile = Database->Prepare<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, std::string, std::string, std::string, MiscFileData>>
			("INSERT OR IGNORE INTO \"Files\" VALUES (\"?\", \"?\", \"?\", \"?\", \"?\", \"?\", \"?\", \"?\");");
	};

	try
	{
		typedef ProtocolClass StaticDataProtocol;
		typedef ProtocolMessageClass<ProtocolVersionClass<StaticDataProtocol>, 
			void(std::string InstanceName, UUID InstanceUUID)> StaticDataV1;

		bfs::path const DatabasePath = RootPath / "." App / "database";
		enum DatabaseVersion()
		{
			V1 = 0,
			End,
			Last = End - 1
		};
			
		bfs::path const TransactionPath = RootPath / "." App / "transactions";

		FilePath = RootPath / "." App / "files";

		if (!bfs::exists(RootPath))
		{
			// Try to create an empty share, since the target didn't exist
			// --
			if (InstanceName.empty())
				{ throw UserError() << "Share '" << RootPath.string() << "' does not exist.  Specify NAME to create a new share."; }
			if (!ValidateFilename(InstanceName))
				{ throw UserError() << "Instance NAME contains invalid characters."; }

			auto RandomGenerator = std::mt19937_64{std::random_device{}()};
			InstanceID = std::uniform_int_distribution<UUID>()(RandomGenerator);
			
			InstanceFilename = GetInstanceFilename(InstanceName, InstanceID);

			// Prepare the base file hierarchy
			bfs::create_directory(RootPath);
			bfs::create_directory(RootPath / "." App);
			bfs::create_directory(FilePath);
			bfs::create_directory(TransactionPath);

			{
				bfs::path StaticDataPath = RootPath / "." App / "static";
				bfs::ofstream Out(StaticDataPath, std::ofstream::out | std::ofstream::binary);
				if (!Out) throw SystemError() << "Could not create file '" << StaticDataPath << "'.";
				auto const &Data = StaticDataV1::Write(InstanceName, InstanceID);
				Out.write((char const *)&Data[0], Data.size());
			}

			{
				bfs::path ReadmePath = RootPath / App "-share-readme.txt";
				bfs::ofstream Out(ReadmePath);
				if (!Out) throw SystemError() << "Could not create file '" << ReadmePath << "'.";
				Out << "Do not modify the contents of this directory.\n\nThis directory is the unmounted data for a " App " share.  Modifying the contents could cause data corruption.  It is safe to move and change the ownership for this folder (but not it's permissions or contents)." << std::endl;
			}

			{
				Database->Execute("CREATE TABLE \"Stats\" "
				"("
					"\"Version\" INTEGER"
				")");
				Database->Execute("INSERT INTO \"Stats\" VALUES (?)", DatabaseVersion::Last);
				Database->Execute("CREATE TABLE \"Counters\" "
				"("
					"\"File\" INTEGER , "
					"\"Change\" INTEGER , "
				")");
				Database->Execute("INSERT INTO \"Counters\" VALUES (?, ?)", 1, 1); // 0 reserved for root
				Database->Execute("CREATE TABLE \"Files\" "
				"("
					"\"Instance\" INTEGER , "
					"\"ID\" INTEGER , "
					"\"ChangeInstance\" INTEGER , "
					"\"ChangeID\" INTEGER , "
					"\"Path\" VARCHAR , "
					"\"Filename\" VARCHAR , "
					"\"Timestamp\" DATETIME , "
					"\"Permissions\" BLOB , "
					"PRIMARY KEY (\"Instance\", \"ID\")"
				")");
				Database->Execute
				(
					"CREATE UNIQUE INDEX \"main\".\"PathIndex\" ON \"Files\" "
					"("
						"\"Path\" ASC, "
						"\"Filename\" ASC"
					")"
				);
				Statement.NewFile->Execute(0, 0, 0, 0, "", "", std::time(), MiscFileData{1, 1, 1, 1, 0, 1, 1, 0, 1, 0});
				Database->Execute("CREATE TABLE \"ancestry\" "
				"("
					"\"Instance\" INTEGER , "
					"\"ID\" INTEGER , "
					"\"ParentInstance\" INTEGER , "
					"\"ParentID\" INTEGER , "
					"PRIMARY KEY (\"Instance\", \"ID\")"
				")");
			}
		}
		else
		{
			// Share exists, restore state
			// --
			if (!InstanceName.empty())
				Log.Warn() << "Share exists, ignoring all other arguments.";

			// Load static data
			bfs::ifstream In(RootPath / "." App / "static", std::ifstream::in | std::ifstream::binary);
			Protocol::Reader<StandardOutLog> Reader(Log);
			Reader.Add<StaticDataV1>([&](std::string &ReadInstanceName, UUID &ReadInstanceUUID) 
			{
				InstanceName = ReadInstanceName;
				InstanceID = ReadInstanceUUID;
			});
			bool Success = Reader.Read(In);
			if (!Success)
				throw SystemError() << "Could not read static data, file may be corrupt.";
			
			InstanceFilename = GetInstanceFilename(InstanceName, InstanceID);

			Database.reset(new SQLDatabase(DatabasePath));
			unsigned int Version = Database->Get<unsigned int>("SELECT \"Version\" FROM \"Stats\"", 0);
			switch (Version)
			{
				default: throw SystemError() << "Unrecognized database version " << Version;
				/*case Last - 2...: Do upgrade to Last - 1;
				case Last - 2...: Do upgrade to Last;*/
				case Last: break;
			}
			Database->Execute("UPDATE \"Stats\" SET \"Version\" = ?", DatabaseVersion::Last);
		}

		Transaction.CreateFile = [this](
			uint64_t const &ID, uint64_t const &ChangeID,
			bfs::path const &Path, bool const &IsFile,
			bool const &OwnerRead, bool const &OwnerWrite, bool const &OwnerExecute,
			bool const &GroupRead, bool const &GroupWrite, bool const &GroupExecute,
			bool const &OtherRead, bool const &OtherWrite, bool const &OtherExecute)
		{
			Statement.CreateFile(ID, InstanceIndex, ChangeID, InstanceIndex, InstanceIndex, 
				Path.parent_path(), Path.filename(), std::time(),
				MiscFileData{OwnerRead, OwnerWrite, OwnerExecute,
					GroupRead, GroupWrite, GroupExecute,
					OtherRead, OtherWrite, OtherExecute,
					IsFile});
			bfs::path const InternalFilename = 
				FilePath / (String() << ID << "-" << InstanceIndex << "-" << ChangeID << "-" << InstanceIndex);
			bfs::ofstream Out(InternalFilename, std::ofstream::out | std::ofstream::binary);
			if (!Out) throw SystemError() << "Could not create file '" << InternalFilename << "'";
		};

		Trans.reset(new Transactor(Mutex, TransactionPath),
			Transact.CreateFile);
	}
	catch (bfs::filesystem_error &Error)
		{ throw SystemError() << Error.what(); }
}

bfs::path ShareCore::GetRoot(void) const { return Root; }

unsigned int ShareCore::GetUser(void) const { return geteuid(); }

unsigned int ShareCore::GetGroup(void) const { return geteuid(); }

std::unique_ptr<ShareFile> ShareCore::Create(bfs::path const &Path, bool IsFile, 
	bool OwnerRead, bool OwnerWrite, bool OwnerExecute,
	bool GroupRead, bool GroupWrite, bool GroupExecute,
	bool OtherRead, bool OtherWrite, bool OtherExecute)
{
	Statements.Begin();
	uint64_t ID = Statements.GetFileID();
	Statements.IncrementFileID();
	Statements.End();
	Statements.Begin();
	uint64_t ChangeID = Statements.GetChangeID();
	Statements.IncrementChangeID();
	Statements.End();
	if (InstanceIndex == 0)
	{
		Statements.AddInstance(Instance, InstanceID);
		InstanceIndex = Statements.GetInstance(Instance, InstanceID);
	}
	Transact(Transact.CreateFile, 
		ID, ChangeID, Path, IsFile,
		OwnerRead, OwnerWrite, OwnerExecute,
		GroupRead, GroupWrite, GroupExecute,
		OtherRead, OtherWrite, OtherExecute);
}

std::unique_ptr<ShareFile> ShareCore::Get(bfs::path const &Path)
{
	if ((Path.size() >= 1) && (Path[0] == 
	auto Values = Statements.GetFileByPath(Path.parent_path(), Path.filename());
	return {new ShareFile(Values)};
}

std::vector<std::unique_ptr<ShareFile>> ShareCore::GetDirectory(ShareFile const &File, unsigned int From, unsigned int Count)
{
	std::unique_ptr<ShareFile> FileAgain = Statements.GetFile(File.ID, File.InstanceIndex);
	Statements.GetFiles((bfs::path(FileAgain.Path) / FileAgain.Filename).string(), 
}
void SetPermissions(ShareFile const &File, 
	bool OwnerRead, bool OwnerWrite, bool OwnerExecute,
	bool GroupRead, bool GroupWrite, bool GroupExecute,
	bool OtherRead, bool OtherWrite, bool OtherExecute);
void SetTimestamp(ShareFile const &File, unsigned int Timestamp);
void Delete(ShareFile const &File);

