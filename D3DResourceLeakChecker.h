#pragma once

// リソースリークチェッカー
// mainの先頭でインスタンスを作ることで、
// すべてのComPtrが解放された後にデストラクタが呼ばれる
struct D3DResourceLeakChecker {
    ~D3DResourceLeakChecker();
};