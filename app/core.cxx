#include "core.h"

#include "protocol.h"
#include "log.h"
#include "shared.h"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

// How about Old Technology File System?

typedef uint64_t UUID;

static bool ValidateFilename(std::string const &Filename)
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
}

Core::Core(std::string const &Root, std::string InstanceName)
{
	try
	{
		boost::filesystem::path RootPath(Root);

		UUID InstanceID = 0;

		typedef ProtocolClass StaticDataProtocol;
		typedef ProtocolMessageClass<ProtocolVersionClass<StaticDataProtocol>, 
			void(std::string InstanceName, UUID InstanceUUID)> StaticDataV1;

		if (!boost::filesystem::exists(RootPath))
		{
			// Try to create an empty share, since the target didn't exist
			// --
			if (InstanceName.empty())
				{ throw UserError() << "Share '" << Root << "' does not exist.  Specify NAME to create a new share."; }
			if (!ValidateFilename(InstanceName))
				{ throw UserError() << "Instance NAME contains invalid characters."; }

			// Generate a uuid for this instance
			{
				auto RandomGenerator = std::mt19937_64{std::random_device{}()};
				InstanceID = std::uniform_int_distribution<UUID>()(RandomGenerator);
			}
			/*std::vector<char> InstanceFilename;
			InstanceFilename.resize(InstanceName.size() + sizeof(InstanceID) * 2 + 2);
			InstanceFilename = InstanceName;
			InstanceFilename += '-';
			for (unsigned int const Offset = 0; Offset < sizeof(InstanceID); ++Offset)
			{
				InstanceFilename[Offset * 2] = 'a' + *(reinterpret_cast<char *>(&InstanceID) + Offset) & 0xF;
				InstanceFilename[Offset * 2 + 1] = 'a' + *(reinterpret_cast<char *>(&InstanceID) + Offset) & 0xF0;
			}
			InstanceFilename[sizeof(InstanceID) * 2] = '_';
			memcpy(&InstanceFilename[0], InstanceName.c_str(), Instance.size());
			InstanceFilename.back() = 0;*/

			// Prepare the base file hierarchy
			boost::filesystem::create_directory(RootPath);
			boost::filesystem::create_directory(RootPath / "." App);
			boost::filesystem::create_directory(RootPath / SplitDir);

			boost::filesystem::path StaticDataPath = RootPath / "." App / "static";
			boost::filesystem::ofstream Out(StaticDataPath, std::ofstream::out | std::ofstream::binary);
			auto const &Data = StaticDataV1::Write(InstanceName, InstanceID);
			if (!Out) throw SystemError() << "Could not create file '" << StaticDataPath << "'.";
			Out.write((char const *)&Data[0], Data.size());
		}
		else
		{
			StandardOutLog Log("Core initialization");

			// Share exists, restore state
			// --
			if (!InstanceName.empty())
				Log.Warn() << "Share exists, ignoring all other arguments.";

			// Load static data
			boost::filesystem::ifstream In(RootPath / "." App / "static", std::ifstream::in | std::ifstream::binary);
			Protocol::Reader<StandardOutLog> Reader(Log);
			Reader.Add<StaticDataV1>([&](std::string &ReadInstanceName, UUID &ReadInstanceUUID) 
			{
				InstanceName = ReadInstanceName;
				InstanceID = ReadInstanceUUID;
			});
			bool Success = Reader.Read(In);
			if (!Success)
				throw SystemError() << "Could not read static data, file may be corrupt.";
		}
	}
	catch (boost::filesystem::filesystem_error &Error)
		{ throw SystemError() << Error.what(); }
}

