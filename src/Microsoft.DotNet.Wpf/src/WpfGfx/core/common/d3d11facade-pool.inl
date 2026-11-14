// 只缓存无 CPU 映射、单 mip、DEFAULT 的临时 RT；键含完整逻辑描述，视图与纹理一起复用。
// 租约由纹理和 surface 包装共同持有，最后一个包装释放后才回池；不延长设备包装寿命。
struct QkTransientBundle {
    D3DSURFACE_DESC desc={};
    DXGI_FORMAT physicalFormat=DXGI_FORMAT_UNKNOWN;
    QkTexturePurpose purpose=QkTexturePurpose::Ordinary;
    std::shared_ptr<QkOpaqueState> opaqueState;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    ULONGLONG bytes=0;
};
static ULONGLONG qkTransientRetainedBytes=0;
struct QkTransientPool {
    bool valid=true;
    std::vector<QkTransientBundle> free;
    static constexpr ULONGLONG MaxBytes=32ULL*1024*1024; // 同进程全部池的逻辑纹理字节上限。
    static constexpr size_t MaxEntries=16; // 每个设备包装的空闲条目上限。
    void Clear() {
        for(auto &item:free) qkTransientRetainedBytes-=item.bytes;
        free.clear();
    }
    void Invalidate() { valid=false; Clear(); }
    ~QkTransientPool() { QkFacadeSharedGuard guard; Clear(); }
    bool Take(const D3DSURFACE_DESC &desc,DXGI_FORMAT physicalFormat,QkTexturePurpose purpose,QkTransientBundle &out) {
        if(!valid) return false;
        for(auto it=free.begin();it!=free.end();++it) {
            auto &d=it->desc;
            if(it->physicalFormat!=physicalFormat || it->purpose!=purpose) continue;
            if(d.Width!=desc.Width || d.Height!=desc.Height || d.Format!=desc.Format || d.Usage!=desc.Usage ||
                d.Pool!=desc.Pool || d.Type!=desc.Type || d.MultiSampleType!=desc.MultiSampleType || d.MultiSampleQuality!=desc.MultiSampleQuality) continue;
            out=std::move(*it); qkTransientRetainedBytes-=out.bytes; free.erase(it);
            QkFacadeCount(QkFacadeCounter::RtPoolHit); return true;
        }
        QkFacadeCount(QkFacadeCounter::RtPoolMiss); return false;
    }
    void Put(QkTransientBundle &&item) noexcept {
        QkFacadeSharedGuard guard;
        if(!valid || !item.texture || item.bytes>MaxBytes) return;
        // FIFO 淘汰本池旧条目；其它设备池占满时直接丢弃，不跨设备转移资源。
        while(!free.empty() && (free.size()>=MaxEntries || qkTransientRetainedBytes+item.bytes>MaxBytes)) {
            qkTransientRetainedBytes-=free.front().bytes; free.erase(free.begin()); QkFacadeCount(QkFacadeCounter::RtPoolEvict);
        }
        if(qkTransientRetainedBytes+item.bytes>MaxBytes) return;
        try { free.push_back(std::move(item)); qkTransientRetainedBytes+=free.back().bytes; QkFacadeCount(QkFacadeCounter::RtPoolReturn); }
        catch(const std::bad_alloc &) { /* 缓存失败只释放资源，不改变原有绘制结果。 */ }
    }
};
struct QkTransientLease {
    std::shared_ptr<QkTransientPool> pool;
    QkTransientBundle bundle;
    ~QkTransientLease() { if(pool) pool->Put(std::move(bundle)); }
};
