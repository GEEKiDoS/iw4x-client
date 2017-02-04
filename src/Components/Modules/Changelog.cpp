#include "STDInclude.hpp"

namespace Components
{
	std::mutex Changelog::Mutex;
	std::vector<std::string> Changelog::Lines;

	void Changelog::LoadChangelog()
	{
		//if (!Changelog::Lines.empty())
		//	return;

		std::lock_guard<std::mutex> _(Changelog::Mutex);
		Changelog::Lines.clear();
		std::string data = Utils::Cache::GetFile("/iw4/changelog.txt");

		if (data.empty())
		{
			data = "^1Unable to get changelog.";
		}

		Changelog::Lines = Utils::String::Explode(data, '\n');

		for (auto& line : Changelog::Lines)
		{
			Utils::String::Replace(line, "\r", "");
		}
	}

	unsigned int Changelog::GetChangelogCount()
	{
		return Changelog::Lines.size();
	}

	// Omit column here
	const char* Changelog::GetChangelogText(unsigned int item, int /*column*/)
	{
		std::lock_guard<std::mutex> _(Changelog::Mutex);
		if (item < Changelog::Lines.size())
		{
			return Utils::String::VA("%s", Changelog::Lines[item].data());
		}

		return "";
	}

	void Changelog::SelectChangelog(unsigned int /*index*/)
	{
		// Don't do anything in here
	}

	Changelog::Changelog()
	{
		std::lock_guard<std::mutex> _(Changelog::Mutex);
		Changelog::Lines.clear();

		// Changelog
		UIFeeder::Add(39.0f, Changelog::GetChangelogCount, Changelog::GetChangelogText, Changelog::SelectChangelog);
	}

	Changelog::~Changelog()
	{
		{
			std::lock_guard<std::mutex> _(Changelog::Mutex);
			Changelog::Lines.clear();
		}
	}
}
