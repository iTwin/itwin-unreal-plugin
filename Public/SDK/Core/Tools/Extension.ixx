export module SDK.Core.Tools:Extension;
import<memory>;
import<unordered_map>;

export namespace SDK::Core::Tools
{
	class Extension{};

	enum ExtensionId: int{}; // define strong type id

	class ExtensionSupport {
	public:
		const std::shared_ptr<Extension>& GetExtension(ExtensionId id);
		void AddExtension(ExtensionId id, const std::shared_ptr<Extension>&);
		bool HasExtension(ExtensionId id);

	private:
		std::unordered_map<ExtensionId, std::shared_ptr<Extension>> extension_;
	};

}