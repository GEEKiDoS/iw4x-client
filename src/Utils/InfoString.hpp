#pragma once

namespace Utils
{
	class InfoString
	{
	public:
		InfoString() {};
		InfoString(const std::string& buffer) : InfoString() { this->parse(buffer); };

		void set(const std::string& key, const std::string& value);
		std::string get(const std::string& key);
		std::string build();

		void dump();

		json11::Json to_json();

	private:
		std::map<std::string, std::string> keyValuePairs;
		void parse(std::string buffer);
	};
}
