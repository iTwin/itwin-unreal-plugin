export module SDK.Core.Network:http;

import <memory>;
import <string>;

import SDK.Core.Json;

export namespace SDK::Core {

	class Http: public std::enable_shared_from_this<Http>
	{
	public:
		static std::shared_ptr<Http> Create();

		void SetBaseUrl(const std::string& url);
		void SetBasicAuth(const std::string& login, const std::string& passwd);
		std::pair<long, std::string> Put(const std::string& url, const std::string& body);
		std::pair<long, std::string> Get(const std::string& url, const std::string& body);
		std::pair<long, std::string> GetJson(const std::string& url, const std::string& body);

		template<typename Type>
		long GetJson(Type& t, const std::string& url, const std::string& body)
		{
			std::pair<long, std::string> r = GetJson(url, body);
			if (r.first == 200)
				t = std::move(SDK::Core::Json::FromString<Type>(r.second));
			return r.first;
		}

		virtual ~Http();
	protected:
		Http();
	};

}
