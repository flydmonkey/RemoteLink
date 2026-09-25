(() => {
  const supported = ["zh-CN", "zh-TW", "en", "ja", "ko"];
  const aliases = {
    zh: "zh-CN",
    "zh-Hans": "zh-CN",
    "zh-Hant": "zh-TW",
    "en-US": "en",
    "en-GB": "en",
    "ja-JP": "ja",
    "ko-KR": "ko",
  };
  const dictionaries = {
    "zh-TW": {
      "登录 RemoteLink": "登入 RemoteLink",
      用户名: "使用者名稱",
      密码: "密碼",
      登录: "登入",
      退出: "登出",
      退出登录: "登出",
      远程桌面连接: "遠端桌面連線",
      "正在连接网关…": "正在連線閘道…",
      选择计算机并输入登录信息: "選擇電腦並輸入登入資訊",
      计算机: "電腦",
      连接: "連線",
      设置: "設定",
      状态: "狀態",
      用户管理: "使用者管理",
      新建用户: "新增使用者",
      显示名称: "顯示名稱",
      初始密码: "初始密碼",
      创建: "建立",
      取消: "取消",
      重置密码: "重設密碼",
      停用: "停用",
      启用: "啟用",
      已启用: "已啟用",
      已停用: "已停用",
      目标主机: "目標主機",
      最近活动: "最近活動",
      服务状态: "服務狀態",
      活动会话: "使用中工作階段",
      信令连接: "信令連線",
      已编码帧: "已編碼影格",
      累计流量: "累計流量",
      累计丢帧: "累計丟幀",
      在线: "線上",
      空闲: "閒置",
      显示分辨率: "顯示解析度",
      语言: "語言",
      跟随系统: "跟隨系統",
      保存设置: "儲存設定",
      设置已保存: "設定已儲存",
      声音: "聲音",
      打印机: "印表機",
      文件管理: "檔案管理",
      全屏: "全螢幕",
      状态信息: "狀態資訊",
      "暂无活动记录。": "暫無活動記錄。",
    },
    en: {
      "登录 RemoteLink": "Sign in to RemoteLink",
      用户名: "Username",
      密码: "Password",
      登录: "Sign in",
      退出: "Sign out",
      退出登录: "Sign out",
      远程桌面连接: "Remote Desktop Connection",
      "正在连接网关…": "Connecting to gateway…",
      选择计算机并输入登录信息: "Select a computer and enter credentials",
      计算机: "Computer",
      连接: "Connect",
      设置: "Settings",
      状态: "Status",
      用户管理: "User management",
      新建用户: "New user",
      显示名称: "Display name",
      初始密码: "Initial password",
      创建: "Create",
      取消: "Cancel",
      重置密码: "Reset password",
      停用: "Disable",
      启用: "Enable",
      已启用: "Enabled",
      已停用: "Disabled",
      目标主机: "Target hosts",
      最近活动: "Recent activity",
      服务状态: "Service status",
      活动会话: "Active sessions",
      信令连接: "Signaling connections",
      已编码帧: "Encoded frames",
      累计流量: "Total traffic",
      累计丢帧: "Dropped frames",
      在线: "Online",
      空闲: "Idle",
      显示分辨率: "Display resolution",
      语言: "Language",
      跟随系统: "Follow system",
      保存设置: "Save settings",
      设置已保存: "Settings saved",
      声音: "Audio",
      打印机: "Printer",
      文件管理: "Files",
      全屏: "Fullscreen",
      状态信息: "Statistics",
      "暂无活动记录。": "No recent activity.",
      清除凭据: "Clear credentials",
      在此浏览器保存这台主机的用户名和密码:
        "Save credentials for this computer in this browser",
      "连接后将在独立会话页面中打开远程桌面。凭据通过当前加密连接发送。":
        "The remote desktop opens in a separate session page. Credentials are sent over the encrypted connection.",
    },
    ja: {
      "登录 RemoteLink": "RemoteLink にログイン",
      用户名: "ユーザー名",
      密码: "パスワード",
      登录: "ログイン",
      退出: "ログアウト",
      退出登录: "ログアウト",
      远程桌面连接: "リモートデスクトップ接続",
      "正在连接网关…": "ゲートウェイに接続中…",
      选择计算机并输入登录信息: "コンピューターを選択してログイン情報を入力",
      计算机: "コンピューター",
      连接: "接続",
      设置: "設定",
      状态: "状態",
      用户管理: "ユーザー管理",
      新建用户: "ユーザーを追加",
      显示名称: "表示名",
      初始密码: "初期パスワード",
      创建: "作成",
      取消: "キャンセル",
      重置密码: "パスワードをリセット",
      停用: "無効化",
      启用: "有効化",
      已启用: "有効",
      已停用: "無効",
      目标主机: "接続先ホスト",
      最近活动: "最近のアクティビティ",
      服务状态: "サービス状態",
      活动会话: "アクティブセッション",
      信令连接: "シグナリング接続",
      已编码帧: "エンコード済みフレーム",
      累计流量: "累計通信量",
      累计丢帧: "累計ドロップ",
      在线: "オンライン",
      空闲: "待機中",
      显示分辨率: "画面解像度",
      语言: "言語",
      跟随系统: "システムに従う",
      保存设置: "設定を保存",
      设置已保存: "設定を保存しました",
      声音: "音声",
      打印机: "プリンター",
      文件管理: "ファイル",
      全屏: "全画面",
      状态信息: "統計情報",
      "暂无活动记录。": "最近のアクティビティはありません。",
    },
    ko: {
      "登录 RemoteLink": "RemoteLink 로그인",
      用户名: "사용자 이름",
      密码: "비밀번호",
      登录: "로그인",
      退出: "로그아웃",
      退出登录: "로그아웃",
      远程桌面连接: "원격 데스크톱 연결",
      "正在连接网关…": "게이트웨이에 연결 중…",
      选择计算机并输入登录信息: "컴퓨터를 선택하고 로그인 정보를 입력하세요",
      计算机: "컴퓨터",
      连接: "연결",
      设置: "설정",
      状态: "상태",
      用户管理: "사용자 관리",
      新建用户: "새 사용자",
      显示名称: "표시 이름",
      初始密码: "초기 비밀번호",
      创建: "만들기",
      取消: "취소",
      重置密码: "비밀번호 재설정",
      停用: "비활성화",
      启用: "활성화",
      已启用: "활성",
      已停用: "비활성",
      目标主机: "대상 호스트",
      最近活动: "최근 활동",
      服务状态: "서비스 상태",
      活动会话: "활성 세션",
      信令连接: "시그널링 연결",
      已编码帧: "인코딩된 프레임",
      累计流量: "누적 트래픽",
      累计丢帧: "누적 드롭 프레임",
      在线: "온라인",
      空闲: "유휴",
      显示分辨率: "화면 해상도",
      语言: "언어",
      跟随系统: "시스템 설정 따르기",
      保存设置: "설정 저장",
      设置已保存: "설정이 저장되었습니다",
      声音: "오디오",
      打印机: "프린터",
      文件管理: "파일",
      全屏: "전체 화면",
      状态信息: "통계",
      "暂无活动记录。": "최근 활동이 없습니다.",
    },
  };
  Object.assign(dictionaries["zh-TW"], { 选择计算机并连接: "選擇電腦並連線" });
  Object.assign(dictionaries.en, {
    选择计算机并连接: "Select a computer and connect",
  });
  Object.assign(dictionaries.ja, {
    选择计算机并连接: "コンピューターを選択して接続",
  });
  Object.assign(dictionaries.ko, {
    选择计算机并连接: "컴퓨터를 선택하고 연결하세요",
  });
  Object.assign(dictionaries["zh-TW"], { "暂无最近活动。": "暫無最近活動。" });
  Object.assign(dictionaries.en, { "暂无最近活动。": "No recent activity." });
  Object.assign(dictionaries.ja, {
    "暂无最近活动。": "最近のアクティビティはありません。",
  });
  Object.assign(dictionaries.ko, { "暂无最近活动。": "최근 활동이 없습니다." });
  Object.assign(dictionaries.en, {
    连接设置: "Connection settings",
    "这些选项会应用到下一次新建的远程会话。":
      "These options apply to the next remote session.",
    界面语言: "Interface language",
    默认跟随当前系统语言: "Uses the current system language by default",
    显示与画面: "Display and video",
    "远程 Windows 桌面的实际尺寸": "Actual size of the remote Windows desktop",
    画面质量: "Video quality",
    "调整 H.264 视频码率": "Adjust the H.264 video bitrate",
    视频编码器: "Video encoder",
    硬件不可用时会自动回退到软件编码:
      "Falls back to software encoding when hardware is unavailable",
    本地资源: "Local resources",
    播放远程声音: "Play remote audio",
    打印机重定向: "Printer redirection",
    "通过 WebRTC 在本机播放 Windows 会话声音":
      "Play Windows session audio locally over WebRTC",
    "将网关打印队列提供给远程 Windows":
      "Expose the gateway print queue to remote Windows",
    "自动 · 当前设备": "Automatic · current device",
    自动选择: "Automatic",
    "硬件加速（实验）": "Hardware acceleration (experimental)",
    "流畅 · 2 Mbps": "Smooth · 2 Mbps",
    "均衡 · 4 Mbps": "Balanced · 4 Mbps",
    "高质量 · 8 Mbps": "High quality · 8 Mbps",
    "超高 · 12 Mbps": "Ultra · 12 Mbps",
  });
  Object.assign(dictionaries["zh-TW"], {
    连接设置: "連線設定",
    "这些选项会应用到下一次新建的远程会话。":
      "這些選項會套用到下一個遠端工作階段。",
    界面语言: "介面語言",
    默认跟随当前系统语言: "預設跟隨系統語言",
    显示与画面: "顯示與畫面",
    "远程 Windows 桌面的实际尺寸": "遠端 Windows 桌面的實際尺寸",
    画面质量: "畫面品質",
    "调整 H.264 视频码率": "調整 H.264 視訊位元率",
    视频编码器: "視訊編碼器",
    本地资源: "本機資源",
    播放远程声音: "播放遠端聲音",
    打印机重定向: "印表機重新導向",
    "自动 · 当前设备": "自動 · 目前裝置",
    自动选择: "自動選擇",
  });
  Object.assign(dictionaries.ja, {
    连接设置: "接続設定",
    "这些选项会应用到下一次新建的远程会话。":
      "これらの設定は次回のリモートセッションに適用されます。",
    界面语言: "表示言語",
    默认跟随当前系统语言: "システム言語に従います",
    显示与画面: "表示と映像",
    "远程 Windows 桌面的实际尺寸": "リモート Windows デスクトップの実サイズ",
    画面质量: "映像品質",
    "调整 H.264 视频码率": "H.264 ビットレートを調整",
    视频编码器: "映像エンコーダー",
    本地资源: "ローカルリソース",
    播放远程声音: "リモート音声を再生",
    打印机重定向: "プリンターリダイレクト",
    "自动 · 当前设备": "自動 · 現在のデバイス",
    自动选择: "自動選択",
  });
  Object.assign(dictionaries.ko, {
    连接设置: "연결 설정",
    "这些选项会应用到下一次新建的远程会话。":
      "이 옵션은 다음 원격 세션부터 적용됩니다.",
    界面语言: "인터페이스 언어",
    默认跟随当前系统语言: "현재 시스템 언어를 사용합니다",
    显示与画面: "디스플레이 및 비디오",
    "远程 Windows 桌面的实际尺寸": "원격 Windows 데스크톱의 실제 크기",
    画面质量: "화질",
    "调整 H.264 视频码率": "H.264 비트레이트 조정",
    视频编码器: "비디오 인코더",
    本地资源: "로컬 리소스",
    播放远程声音: "원격 오디오 재생",
    打印机重定向: "프린터 리디렉션",
    "自动 · 当前设备": "자동 · 현재 장치",
    自动选择: "자동 선택",
  });
  Object.assign(dictionaries["zh-TW"], {
    "RemoteLink · 设置": "RemoteLink · 設定",
    设置: "設定",
    连接设置: "連線設定",
    "这些选项会应用到下一次新建的远程会话。":
      "這些選項會套用到下一個新建的遠端工作階段。",
    语言: "語言",
    界面语言: "介面語言",
    默认跟随当前系统语言: "預設跟隨目前的系統語言",
    跟随系统: "跟隨系統",
    简体中文: "簡體中文",
    显示与画面: "顯示與畫面",
    显示分辨率: "顯示解析度",
    "远程 Windows 桌面的实际尺寸": "遠端 Windows 桌面的實際尺寸",
    "自动 · 当前设备": "自動 · 目前裝置",
    画面质量: "畫面品質",
    "调整 H.264 视频码率": "調整 H.264 視訊位元率",
    "流畅 · 2 Mbps": "流暢 · 2 Mbps",
    "均衡 · 4 Mbps": "均衡 · 4 Mbps",
    "高质量 · 8 Mbps": "高品質 · 8 Mbps",
    "超高 · 12 Mbps": "超高 · 12 Mbps",
    视频编码器: "視訊編碼器",
    硬件不可用时会自动回退到软件编码: "硬體無法使用時會自動切換至軟體編碼",
    自动选择: "自動選擇",
    "软件 · OpenH264": "軟體 · OpenH264",
    "硬件加速（实验）": "硬體加速（實驗）",
    本地资源: "本機資源",
    播放远程声音: "播放遠端聲音",
    "通过 WebRTC 在本机播放 Windows 会话声音":
      "透過 WebRTC 在本機播放 Windows 工作階段的聲音",
    打印机重定向: "印表機重新導向",
    "将网关打印队列提供给远程 Windows": "將閘道列印佇列提供給遠端 Windows",
    "浏览器首次播放声音时可能需要点击一次远程画面。打印任务将由网关的 PDF 打印队列接收。":
      "瀏覽器首次播放聲音時可能需要點選一次遠端畫面。列印工作將由閘道的 PDF 列印佇列接收。",
    设置已保存: "設定已儲存",
    保存设置: "儲存設定",
    返回: "返回",
    返回连接页面: "返回連線頁面",
    状态: "狀態",
    应用导航: "應用程式導覽",
  });
  Object.assign(dictionaries.en, {
    "RemoteLink · 设置": "RemoteLink · Settings",
    设置: "Settings",
    连接设置: "Connection settings",
    "这些选项会应用到下一次新建的远程会话。":
      "These options apply to the next remote session.",
    语言: "Language",
    界面语言: "Interface language",
    默认跟随当前系统语言: "Uses the current system language by default",
    跟随系统: "Follow system",
    简体中文: "Simplified Chinese",
    显示与画面: "Display and video",
    显示分辨率: "Display resolution",
    "远程 Windows 桌面的实际尺寸": "Actual size of the remote Windows desktop",
    "自动 · 当前设备": "Automatic · current device",
    画面质量: "Video quality",
    "调整 H.264 视频码率": "Adjust the H.264 video bitrate",
    "流畅 · 2 Mbps": "Smooth · 2 Mbps",
    "均衡 · 4 Mbps": "Balanced · 4 Mbps",
    "高质量 · 8 Mbps": "High quality · 8 Mbps",
    "超高 · 12 Mbps": "Ultra · 12 Mbps",
    视频编码器: "Video encoder",
    硬件不可用时会自动回退到软件编码:
      "Falls back to software encoding when hardware is unavailable",
    自动选择: "Automatic",
    "软件 · OpenH264": "Software · OpenH264",
    "硬件加速（实验）": "Hardware acceleration (experimental)",
    本地资源: "Local resources",
    播放远程声音: "Play remote audio",
    "通过 WebRTC 在本机播放 Windows 会话声音":
      "Play Windows session audio locally over WebRTC",
    打印机重定向: "Printer redirection",
    "将网关打印队列提供给远程 Windows":
      "Expose the gateway print queue to remote Windows",
    "浏览器首次播放声音时可能需要点击一次远程画面。打印任务将由网关的 PDF 打印队列接收。":
      "Click the remote desktop once if the browser blocks initial audio. Print jobs are received by the gateway PDF queue.",
    设置已保存: "Settings saved",
    保存设置: "Save settings",
    返回: "Back",
    返回连接页面: "Back to connection page",
    状态: "Status",
    应用导航: "App navigation",
  });
  Object.assign(dictionaries.ja, {
    "RemoteLink · 设置": "RemoteLink · 設定",
    设置: "設定",
    连接设置: "接続設定",
    "这些选项会应用到下一次新建的远程会话。":
      "これらの設定は次回のリモートセッションに適用されます。",
    语言: "言語",
    界面语言: "表示言語",
    默认跟随当前系统语言: "既定では現在のシステム言語に従います",
    跟随系统: "システムに従う",
    简体中文: "簡体字中国語",
    显示与画面: "表示と映像",
    显示分辨率: "画面解像度",
    "远程 Windows 桌面的实际尺寸": "リモート Windows デスクトップの実サイズ",
    "自动 · 当前设备": "自動 · 現在のデバイス",
    画面质量: "映像品質",
    "调整 H.264 视频码率": "H.264 映像ビットレートを調整",
    "流畅 · 2 Mbps": "スムーズ · 2 Mbps",
    "均衡 · 4 Mbps": "標準 · 4 Mbps",
    "高质量 · 8 Mbps": "高品質 · 8 Mbps",
    "超高 · 12 Mbps": "最高 · 12 Mbps",
    视频编码器: "映像エンコーダー",
    硬件不可用时会自动回退到软件编码:
      "ハードウェアが利用できない場合はソフトウェアエンコードに切り替えます",
    自动选择: "自動選択",
    "软件 · OpenH264": "ソフトウェア · OpenH264",
    "硬件加速（实验）": "ハードウェアアクセラレーション（試験）",
    本地资源: "ローカルリソース",
    播放远程声音: "リモート音声を再生",
    "通过 WebRTC 在本机播放 Windows 会话声音":
      "WebRTC 経由で Windows セッションの音声をこの端末で再生します",
    打印机重定向: "プリンターリダイレクト",
    "将网关打印队列提供给远程 Windows":
      "ゲートウェイの印刷キューをリモート Windows に提供します",
    "浏览器首次播放声音时可能需要点击一次远程画面。打印任务将由网关的 PDF 打印队列接收。":
      "初回の音声再生時はリモート画面のクリックが必要な場合があります。印刷ジョブはゲートウェイの PDF 印刷キューが受信します。",
    设置已保存: "設定を保存しました",
    保存设置: "設定を保存",
    返回: "戻る",
    返回连接页面: "接続ページに戻る",
    状态: "状態",
    应用导航: "アプリナビゲーション",
  });
  Object.assign(dictionaries.ko, {
    "RemoteLink · 设置": "RemoteLink · 설정",
    设置: "설정",
    连接设置: "연결 설정",
    "这些选项会应用到下一次新建的远程会话。":
      "이 옵션은 다음 원격 세션부터 적용됩니다.",
    语言: "언어",
    界面语言: "인터페이스 언어",
    默认跟随当前系统语言: "기본적으로 현재 시스템 언어를 사용합니다",
    跟随系统: "시스템 설정 따르기",
    简体中文: "중국어 간체",
    显示与画面: "디스플레이 및 비디오",
    显示分辨率: "화면 해상도",
    "远程 Windows 桌面的实际尺寸": "원격 Windows 데스크톱의 실제 크기",
    "自动 · 当前设备": "자동 · 현재 장치",
    画面质量: "화질",
    "调整 H.264 视频码率": "H.264 비디오 비트레이트 조정",
    "流畅 · 2 Mbps": "부드럽게 · 2 Mbps",
    "均衡 · 4 Mbps": "균형 · 4 Mbps",
    "高质量 · 8 Mbps": "고화질 · 8 Mbps",
    "超高 · 12 Mbps": "최고 화질 · 12 Mbps",
    视频编码器: "비디오 인코더",
    硬件不可用时会自动回退到软件编码:
      "하드웨어를 사용할 수 없으면 소프트웨어 인코딩으로 전환됩니다",
    自动选择: "자동 선택",
    "软件 · OpenH264": "소프트웨어 · OpenH264",
    "硬件加速（实验）": "하드웨어 가속(실험적)",
    本地资源: "로컬 리소스",
    播放远程声音: "원격 오디오 재생",
    "通过 WebRTC 在本机播放 Windows 会话声音":
      "WebRTC를 통해 Windows 세션 오디오를 이 장치에서 재생합니다",
    打印机重定向: "프린터 리디렉션",
    "将网关打印队列提供给远程 Windows":
      "게이트웨이 인쇄 대기열을 원격 Windows에 제공합니다",
    "浏览器首次播放声音时可能需要点击一次远程画面。打印任务将由网关的 PDF 打印队列接收。":
      "브라우저에서 처음 오디오를 재생할 때 원격 화면을 한 번 클릭해야 할 수 있습니다. 인쇄 작업은 게이트웨이 PDF 인쇄 대기열에서 수신합니다.",
    设置已保存: "설정이 저장되었습니다",
    保存设置: "설정 저장",
    返回: "뒤로",
    返回连接页面: "연결 페이지로 돌아가기",
    状态: "상태",
    应用导航: "앱 탐색",
  });
  function systemLanguage() {
    const raw = navigator.languages?.[0] || navigator.language || "zh-CN";
    if (
      raw.startsWith("zh-TW") ||
      raw.startsWith("zh-HK") ||
      raw.startsWith("zh-MO") ||
      raw.includes("Hant")
    )
      return "zh-TW";
    if (raw.startsWith("zh")) return "zh-CN";
    if (raw.startsWith("ja")) return "ja";
    if (raw.startsWith("ko")) return "ko";
    return "en";
  }
  const preference = localStorage.getItem("remotelink-language") || "system";
  const language =
    preference === "system"
      ? systemLanguage()
      : aliases[preference] || preference;
  document.documentElement.lang = language;
  function translate(root = document) {
    const dict = dictionaries[language] || {};
    const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
    const nodes = [];
    while (walker.nextNode()) nodes.push(walker.currentNode);
    for (const node of nodes) {
      if (node.parentElement?.closest("script,style")) continue;
      const raw = node.nodeValue,
        trimmed = raw.trim(),
        translated = dict[trimmed];
      if (translated) node.nodeValue = raw.replace(trimmed, translated);
    }
    for (const element of root.querySelectorAll?.(
      "[title],[aria-label],[placeholder]",
    ) || []) {
      for (const attribute of ["title", "aria-label", "placeholder"]) {
        const value = element.getAttribute(attribute);
        if (value && dict[value]) element.setAttribute(attribute, dict[value]);
      }
    }
  }
  translate();
  new MutationObserver((records) => {
    for (const record of records)
      for (const node of record.addedNodes)
        if (node.nodeType === 1 || node.nodeType === 3)
          translate(node.nodeType === 1 ? node : node.parentElement);
  }).observe(document.documentElement, { childList: true, subtree: true });
  window.RemoteLinkI18n = {
    language,
    preference,
    supported,
    setLanguage(value) {
      localStorage.setItem("remotelink-language", value);
      location.reload();
    },
  };
})();
