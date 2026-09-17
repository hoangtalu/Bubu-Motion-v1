#ifndef ASSETS_H
#define ASSETS_H

#include <string>
#include <functional>
#include <memory>

#include <cJSON.h>
#include <esp_partition.h>
#include <model_path.h>
#include <map>
#include <string>

#if HAVE_LVGL
#include <spi_flash_mmap.h>
#endif

struct Asset {
    size_t size;
    size_t offset;
};

class Assets {
public:
    static Assets& GetInstance() {
        static Assets instance;
        return instance;
    }
    ~Assets();

    /* expected_sha256: lowercase hex digest of the whole bundle, from the server.
     * Empty means "no hash offered" and only the bundle's own checksum applies --
     * which is a 16-bit additive sum (LvglStrategy::CalculateChecksum), blind to
     * byte reordering and with a 1-in-65536 miss rate on a 1.3 MB binary. Pass a
     * hash whenever the server has one. */
    bool Download(std::string url, const std::string& expected_sha256,
                  std::function<void(int progress, size_t speed)> progress_callback);
    bool Apply();
    bool GetAssetData(const std::string& name, void*& ptr, size_t& size);

    /* False from the moment UnApplyPartition() unmaps the partition (Download()
     * calls it first thing, before streaming a byte) until InitializePartition()
     * next succeeds -- which may be never, if that download then fails (e.g. a
     * sha256 mismatch): UnApplyPartition() has no way to put the old mapping
     * back, so a failed download leaves this false for the rest of the boot.
     * Every previously resolved menu/care icon is a raw pointer straight into
     * that mmap (ResolvePersistentAssetImage in menu_system.cc caches it once
     * at boot rather than re-fetching per draw), so once this is false those
     * pointers are dangling -- LVGL redrawing one is a Cache error / MMU fault,
     * not a benign glitch. Callers that would make an icon-bearing screen
     * visible (MenuSystem::Open(), the only entry point from the idle eyes
     * screen into any menu panel) must check this first and no-op instead. */
    inline bool partition_valid() const { return partition_valid_; }
    inline std::string default_assets_url() const { return default_assets_url_; }

private:
    Assets();
    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;

    bool InitializePartition();
    void UnApplyPartition();
    static bool FindPartition(Assets* assets);
    static bool LoadSrmodelsFromIndex(Assets* assets, cJSON* root = nullptr);
  
    class AssetStrategy {
    public:
        virtual ~AssetStrategy() = default;
        virtual bool Apply(Assets* assets) = 0;
        virtual bool InitializePartition(Assets* assets) = 0;
        virtual void UnApplyPartition(Assets* assets) = 0;
        virtual bool GetAssetData(Assets* assets, const std::string& name, void*& ptr, size_t& size) = 0;
    };
    
    class LvglStrategy : public AssetStrategy {
    public:
        bool Apply(Assets* assets) override;
        bool InitializePartition(Assets* assets) override;
        void UnApplyPartition(Assets* assets) override;
        bool GetAssetData(Assets* assets, const std::string& name, void*& ptr, size_t& size) override;
    private:
        static uint32_t CalculateChecksum(const char* data, uint32_t length);
        std::map<std::string, Asset> assets_;
        esp_partition_mmap_handle_t mmap_handle_ = 0;
        const char* mmap_root_ = nullptr;
        bool checksum_valid_ = false;
    };
    
    class EmoteStrategy : public AssetStrategy {
    public:
        bool Apply(Assets* assets) override;
        bool InitializePartition(Assets* assets) override;
        void UnApplyPartition(Assets* assets) override;
        bool GetAssetData(Assets* assets, const std::string& name, void*& ptr, size_t& size) override;
    };
    
    // Strategy instance
    std::unique_ptr<AssetStrategy> strategy_;

protected:
    const esp_partition_t* partition_ = nullptr;
    bool partition_valid_ = false;
    std::string default_assets_url_;
    srmodel_list_t* models_list_ = nullptr;
};

#endif
