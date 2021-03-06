#include "StdAfx.h"
#include "GlobalUnits.h"
#include "PlatformFrame.h"
#include "PlatformEvent.h"
#include "ServerListView.h"
#include ".\serverlistview.h"

// sort定义值发生了变化
#define  SORTID(sortid)		(sortid / 100)
#define  HOTLEVEL(sortid)	((sortid % 100) / 10)
#define  STARLEVEL(sortid)  ((sortid % 100) % 10)


//字符串分割
void SplitString( CString strSrc, const TCHAR chSplit, CStringArray& strDestArray )
{
	strDestArray.RemoveAll(); // 清空目标字符串

	if ( strSrc.GetLength() == 0 )  {
		// 长度为0，不做处理
		return;
	}

	CString strTemp;    
	strTemp.Empty();
	strSrc += chSplit;

	for ( long i = 0; i < strSrc.GetLength(); i++ ) {
		TCHAR ch = strSrc.GetAt( i );
		if ( chSplit == ch ) {
			strDestArray.Add( strTemp );
			strTemp.Empty();
			continue;
		}

		strTemp += ch;
	}
}
//////////////////////////////////////////////////////////////////////////////////

//常量定义
#define ITEM_SIZE					16									//子项高度
#define ITEM_HEIGHT					27									//子项高度

//////////////////////////////////////////////////////////////////////////////////

//时间定义
#define IDI_UPDATE_ONLINE			100									//更新人数
#define TIME_UPDATE_ONLINE			10000								//更新人数

//////////////////////////////////////////////////////////////////////////////////

//图标索引
#define IND_ROOT					0									//列表顶项
#define IND_TYPE					1									//游戏类型
#define IND_NODE					2									//游戏节点
#define IND_KIND_NODOWN				3									//还没下载
#define IND_KIND_UNKNOW				4									//未知类型
#define IND_SERVER_UPDATE			5									//收藏房间
#define IND_SERVER_ATTACH			6									//收藏房间
#define IND_SERVER_NORMAL			7									//正常房间
#define IND_SERVER_ENTRANCE			8									//当前使用
#define IND_SERVER_EVERENTRANCE		9									//曾经房间
#define IND_BROWSE					10									//浏览子项
#define IND_FUNCTION				11									//功能子项

//////////////////////////////////////////////////////////////////////////////////

#define SHOW_LAST_COUNT             3                                   //最近游戏数

//////////////////////////////////////////////////////////////////////////////////

BEGIN_MESSAGE_MAP(CServerListView, CTreeCtrl)

	//系统消息
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_TIMER()
	ON_WM_SETCURSOR()
	ON_WM_ERASEBKGND()
	ON_NOTIFY_REFLECT(NM_CLICK, OnNMLClick)
	ON_NOTIFY_REFLECT(NM_RCLICK, OnNMRClick)
	ON_NOTIFY_REFLECT(TVN_SELCHANGED, OnTvnSelchanged)
//	ON_NOTIFY_REFLECT(TVN_ITEMEXPANDING, OnTvnItemexpanding)

	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
END_MESSAGE_MAP()

//////////////////////////////////////////////////////////////////////////////////

//构造函数
CServerListView::CServerListView()
{
	//设置变量
	m_nXScroll=0;
	m_nYScroll=0;

	//列表句柄
	m_TreeListRoot=NULL;
	m_TreeAssistant=NULL;

	//辅助变量
	m_hItemMouseHover=NULL;
	m_hTreeClickExpand=NULL;

	//内核变量
	m_GameImageMap.InitHashTable(PRIME_KIND);

	//加载资源
	m_ImageTypeBkgnd.LoadFromResource(AfxGetInstanceHandle(),IDB_SERVERITEM_BKGND);
	m_ImageArrow1.LoadImage(AfxGetInstanceHandle(),TEXT("KIND_TYPE_ARROW"));
	m_ImageArrow2.LoadImage(AfxGetInstanceHandle(),TEXT("SERVER_LIST_ARROW"));
	m_ImageStarLevel.LoadImage(AfxGetInstanceHandle(),TEXT("STAR_LEVEL"));
	m_ImageHotLevel.LoadImage(AfxGetInstanceHandle(),TEXT("HOT_LEVEL"));
	m_ImageItemLine.LoadImage(AfxGetInstanceHandle(),TEXT("ITEM_LINE"));
	m_ImageVolumnFlag.LoadImage(AfxGetInstanceHandle(),TEXT("VOLUMN_FLAG"));

	//创建字体
	m_FontBold.CreateFont(-12,0,0,0,600,0,0,0,134,3,2,ANTIALIASED_QUALITY,2,TEXT("宋体"));
	m_FontUnderLine.CreateFont(-12,0,0,0,300,0,1,0,134,3,2,ANTIALIASED_QUALITY,2,TEXT("宋体"));
	m_FontNormal.CreateFont(-12,0,0,0,300,0,0,0,134,3,2,ANTIALIASED_QUALITY,2,TEXT("宋体"));

	//设置变量
//	m_LastServerItem.m_GameType.wTypeID=0;
//	m_LastServerItem.m_GameType.wSortID=0;
//	m_LastServerItem.m_GameType.wJoinID=0;
//	_sntprintf(m_LastServerItem.m_GameType.szTypeName, CountArray(m_LastServerItem.m_GameType.szTypeName), TEXT("最近游戏"));

	return;
}

//析构函数
CServerListView::~CServerListView()
{
	//保存记录
	SaveLastPlayGame();
}

//命令函数
BOOL CServerListView::OnCommand(WPARAM wParam, LPARAM lParam)
{
	//变量定义
	/*UINT nCommandID=LOWORD(wParam);

	//菜单命令
	switch (nCommandID)
	{
	case IDM_ENTER_SERVER:		//进入房间
		{
			//获取树项
			HTREEITEM hTreeItem=GetSelectedItem();

			//选择判断
			if (hTreeItem==NULL) return TRUE;

			//获取数据
			CGameListItem * pGameListItem=(CGameListItem *)GetItemData(hTreeItem);

			//命令处理
			if (pGameListItem!=NULL)
			{
				switch (pGameListItem->GetItemGenre())
				{
				case ItemGenre_Kind:	//游戏种类
					{
						//进入房间
						CPlatformFrame * pPlatformFrame=CPlatformFrame::GetInstance();
						pPlatformFrame->EntranceServerItem((CGameKindItem *)pGameListItem);

						break;
					}
				case ItemGenre_Server:	//游戏房间
					{
						//进入房间
						CPlatformFrame * pPlatformFrame=CPlatformFrame::GetInstance();
						pPlatformFrame->EntranceServerItem((CGameServerItem *)pGameListItem);

						break;
					}
				}
			}

			return TRUE;
		}
	case IDM_SHRINK_LIST:		//收缩列表
		{
			//收缩列表
			HTREEITEM hCurrentItem=GetSelectedItem();
			if (hCurrentItem!=NULL)	Expand(hCurrentItem,TVE_COLLAPSE);

			return TRUE;
		}
	case IDM_EXPAND_LIST:		//展开列表
		{
			//展开列表
			HTREEITEM hCurrentItem=GetSelectedItem();
			if (hCurrentItem!=NULL)	Expand(hCurrentItem,TVE_EXPAND);

			return TRUE;
		}
	}*/

	return __super::OnCommand(wParam,lParam);
}

//窗口函数
LRESULT CServerListView::DefWindowProc(UINT uMessage, WPARAM wParam, LPARAM lParam)
{
	//双击消息
	switch (uMessage)
	{
	case WM_LBUTTONDOWN:		//鼠标消息
		{
			//鼠标坐标
/*			CPoint MousePoint;
			MousePoint.SetPoint(LOWORD(lParam),HIWORD(lParam));

			//设置变量
			m_hTreeClickExpand=NULL;

			//点击测试
			HTREEITEM hTreeItem=HitTest(MousePoint);

			//消息处理
			if ((hTreeItem!=NULL)&&(GetChildItem(hTreeItem)!=NULL))
			{
				//选择树项
				SetFocus();
				Select(hTreeItem,TVGN_CARET);

				//获取位置
				CRect rcTreeItem;
				GetItemRect(hTreeItem,&rcTreeItem,TRUE);

				//消息处理
				if (rcTreeItem.PtInRect(MousePoint)==TRUE)
				{
					//展开列表
					if (ExpandVerdict(hTreeItem)==false)
					{
						m_hTreeClickExpand=hTreeItem;
						Expand(m_hTreeClickExpand,TVE_EXPAND);
					}

					return 0;
				}
			}*/

			break;
		}
	case WM_LBUTTONDBLCLK:		//鼠标消息
		{
			//鼠标坐标
/*			CPoint MousePoint;
			MousePoint.SetPoint(LOWORD(lParam),HIWORD(lParam));

			//点击测试
			HTREEITEM hTreeItem=HitTest(MousePoint);

			//下载判断
			if (hTreeItem!=NULL)
			{
				//获取数据
				CGameListItem * pGameListItem=(CGameListItem *)GetItemData(hTreeItem);
				if ((pGameListItem!=NULL)&&(pGameListItem->GetItemGenre()==ItemGenre_Kind))
				{
					//变量定义
					CGameKindItem * pGameKindItem=(CGameKindItem *)pGameListItem;

					//下载判断
					if (pGameKindItem->m_dwProcessVersion==0L)
					{
						//获取版本
						tagGameKind * pGameKind=&pGameKindItem->m_GameKind;
						CWHService::GetModuleVersion(pGameKind->szProcessName,pGameKindItem->m_dwProcessVersion);

						//下载判断
						if (pGameKindItem->m_dwProcessVersion==0L)
						{
							CGlobalUnits * pGlobalUnits=CGlobalUnits::GetInstance();
							pGlobalUnits->DownLoadClient(pGameKind->szKindName,pGameKind->wKindID,0);
						}
						else
						{
							OnGameItemUpdate(pGameKindItem);
						}
					}
				}
			}*/

			//消息处理
/*			if ((hTreeItem!=NULL)&&(GetChildItem(hTreeItem)!=NULL))
			{
				//选择树项
				SetFocus();
				Select(hTreeItem,TVGN_CARET);

				//位置判断
				CRect rcTreeItem;
				GetItemRect(hTreeItem,&rcTreeItem,TRUE);
				if (rcTreeItem.PtInRect(MousePoint)==FALSE) break;

				//展开判断
				if ((m_hTreeClickExpand!=hTreeItem)&&(ExpandVerdict(hTreeItem)==true))
				{
					//设置变量
					m_hTreeClickExpand=NULL;

					//展开列表
					Expand(hTreeItem,TVE_COLLAPSE);
				}

				return 0;
			}*/

			break;
		}
	}

	return __super::DefWindowProc(uMessage,wParam,lParam);
}

//快速通道
VOID CServerListView::InitAssistantItem()
{
	//工作目录
	TCHAR szDirectory[MAX_PATH]=TEXT("");
	CWHService::GetWorkDirectory(szDirectory,CountArray(szDirectory));

	//构造路径
	TCHAR szAssistantPath[MAX_PATH]=TEXT("");
	_sntprintf(szAssistantPath,CountArray(szAssistantPath),TEXT("%s\\Collocate\\Collocate.INI"),szDirectory);

	//读取名字
	TCHAR szAssistant[512]=TEXT("");
	GetPrivateProfileString(TEXT("Assistant"),TEXT("AssistantName"),TEXT(""),szAssistant,CountArray(szAssistant),szAssistantPath);

	//创建子项
	if (szAssistant[0]!=0)
	{
		//变量定义
		DWORD dwInsideID=1;

		//创建树项
		UINT nMask=TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_TEXT|TVIF_PARAM|TVIF_STATE;
		m_TreeAssistant=InsertItem(nMask,szAssistant,IND_FUNCTION,IND_FUNCTION,TVIS_BOLD,TVIS_BOLD,NULL,NULL,TVI_LAST);

		//创建子项
		do
		{
			//构造键名
			TCHAR szItemName[16]=TEXT("");
			_sntprintf(szItemName,CountArray(szItemName),TEXT("Assistant%ld"),dwInsideID);

			//读取连接
			TCHAR szAssistantName[128]=TEXT("");
			GetPrivateProfileString(szItemName,TEXT("AssistantName"),TEXT(""),szAssistantName,CountArray(szAssistantName),szAssistantPath);

			//完成判断
			if (szAssistantName[0]==0) break;

			//插入子项
			InsertInsideItem(szAssistantName,IND_BROWSE,dwInsideID++,m_TreeAssistant);

		} while (true);

		//展开列表
		ExpandListItem(m_TreeAssistant);
	}

	return;
}

//配置函数
VOID CServerListView::InitServerTreeView()
{
	//设置属性
	SetItemHeight(ITEM_HEIGHT);
	ModifyStyle(0,TVS_HASBUTTONS|TVS_HASLINES|TVS_SHOWSELALWAYS|TVS_TRACKSELECT|TVS_FULLROWSELECT|TVS_HASLINES);

	//加载标志
	CBitmap ServerImage;
	ServerImage.LoadBitmap(IDB_SERVER_LIST_IMAGE);
	m_ImageList.Create(ITEM_SIZE,ITEM_SIZE,ILC_COLOR16|ILC_MASK,0,0);

	//设置资源
	SetImageList(&m_ImageList,LVSIL_NORMAL);
	m_ImageList.Add(&ServerImage,RGB(255,0,255));

	//设置树项
	// iii 去掉关于产品作为root的条目
	//m_TreeListRoot=InsertInsideItem(szProduct,IND_ROOT,0,TVI_ROOT);
	m_TreeListRoot = NULL;

	//常规插入
	HTREEITEM hTreeItemNormal=m_TreeListRoot;
//	LPCTSTR pszTypeName=m_LastServerItem.m_GameType.szTypeName;
//	m_LastServerItem.m_hTreeItemNormal=InsertGameListItem(pszTypeName,IND_TYPE,&m_LastServerItem,hTreeItemNormal);

	//载入记录
	LoadLastPlayGame();

	//快速通道
	InitAssistantItem();

	//设置滚动
	m_SkinScrollBar.InitScroolBar(this);

	//设置字体
	SetFont(&CSkinResourceManager::GetInstance()->GetDefaultFont());

	return;
}

//获取选择
HTREEITEM CServerListView::GetCurrentSelectItem(bool bOnlyText)
{
	//获取光标
	CPoint MousePoint;
	GetCursorPos(&MousePoint);
	ScreenToClient(&MousePoint);

	//点击判断
	HTREEITEM hTreeItem=HitTest(MousePoint);

	if (bOnlyText==true)
	{
		//获取位置
		CRect rcTreeItem;
		GetItemRect(hTreeItem,&rcTreeItem,TRUE);

		//选择判断
		if (rcTreeItem.PtInRect(MousePoint)==FALSE) return NULL;
	}

	return hTreeItem;
}

//添加最后游戏
void CServerListView::AddLastPlayGame(WORD wServerID, CGameServerItem *pGameServerItem)
{
	//参数校验
	if(pGameServerItem==NULL) return;

	//查找记录
	POSITION Position=m_LastPlayGameList.GetHeadPosition();
	while(Position != NULL)
	{
		POSITION temppos=Position;
		if(m_LastPlayGameList.GetNext(Position) == wServerID)
		{
			m_LastPlayGameList.RemoveAt(temppos);
			break;
		}
	}

	//记录游戏
	m_LastPlayGameList.AddHead(wServerID);

	//删除子项
	if(m_LastPlayGameList.GetCount() > SHOW_LAST_COUNT)
	{
		//删除记录
		WORD wDeleServerID=m_LastPlayGameList.GetTail();
		m_LastPlayGameList.RemoveTail();

		//删除子项
// 		HTREEITEM hDeleParentItem=EmunGameServerItem(m_LastServerItem.m_hTreeItemNormal,wDeleServerID);
// 		if(hDeleParentItem)
// 		{
// 			DeleteItem(hDeleParentItem);
// 		}
		if (m_pILastGameServerSink)
		{
			m_pILastGameServerSink->OnRemoveLastPlayGame(wDeleServerID);
		}
	}

	//插入房间
//	if(EmunGameServerItem(m_LastServerItem.m_hTreeItemNormal,wServerID)==NULL)
//	{
		//插入处理
//		TCHAR szTitle[64]=TEXT("");
//		UINT uImageIndex=GetGameImageIndex(pGameServerItem);
//		GetGameItemTitle(pGameServerItem,szTitle,CountArray(szTitle));

		//最近插入
//		InsertGameListItem(szTitle,uImageIndex,pGameServerItem,m_LastServerItem.m_hTreeItemNormal);
		if (m_pILastGameServerSink)
		{
			m_pILastGameServerSink->OnAddLastPlayGame(wServerID, pGameServerItem);
		}

		//展开列表
//		ExpandListItem(&m_LastServerItem);
//	}
}

//判断最后游戏
bool CServerListView::IsLastPlayGame(WORD wServerID)
{
	//查找游戏房间
	POSITION Position=m_LastPlayGameList.GetHeadPosition();
	while(Position != NULL)
	{
		if(m_LastPlayGameList.GetNext(Position) == wServerID) return true;
	}

	return false;
}

//枚举记录
HTREEITEM CServerListView::EmunGameServerItem(HTREEITEM hParentItem, WORD wServerID)
{
	ASSERT(hParentItem != NULL);

	HTREEITEM hTreeItem = GetChildItem(hParentItem);

	//选择判断
	while(hTreeItem)
	{
		//获取数据
		CGameListItem * pGameListItem=(CGameListItem *)GetItemData(hTreeItem);

		//判断记录
		if ((pGameListItem!=NULL)&&(pGameListItem->GetItemGenre()==ItemGenre_Server))
		{
			//
			if(((CGameServerItem *)pGameListItem)->m_GameServer.wServerID == wServerID)
			{
				return hTreeItem;
			}
		}

		//下一记录
		hTreeItem = GetNextSiblingItem(hTreeItem);
	}

	return NULL;
}

//子项为空
bool CServerListView::IsEmptySubitem(WORD wKindID)
{
	//获取类型
	CServerListData * pServerListData=CServerListData::GetInstance();
	CGameKindItem * pGameKindItem=pServerListData->SearchGameKind(wKindID);

	//子项判断
	if(pGameKindItem!=NULL)
	{
		if(pGameKindItem->m_hTreeItemNormal!=NULL)
		{
			return (GetChildItem(pGameKindItem->m_hTreeItemNormal)==NULL);
		}
	}

	return false;
}

//展开状态
bool CServerListView::ExpandVerdict(HTREEITEM hTreeItem)
{
	if (hTreeItem!=NULL)
	{
		UINT uState=GetItemState(hTreeItem,TVIS_EXPANDED);
		return ((uState&TVIS_EXPANDED)!=0);
	}

	return false;
}

//展开列表
bool CServerListView::ExpandListItem(HTREEITEM hTreeItem)
{
	//效验参数
	ASSERT(hTreeItem!=NULL);
	if (hTreeItem==NULL) return false;

	//展开列表
	HTREEITEM hCurrentItem=hTreeItem;
	do
	{
		Expand(hCurrentItem,TVE_EXPAND);
		hCurrentItem=GetParentItem(hCurrentItem);
	} while (hCurrentItem!=NULL);

	return true;
}

//展开列表
bool CServerListView::ExpandListItem(CGameListItem * pGameListItem)
{
	//效验参数
	ASSERT(pGameListItem!=NULL);
	if (pGameListItem==NULL) return false;

	//展开树项
	if (pGameListItem->m_hTreeItemAttach!=NULL) ExpandListItem(pGameListItem->m_hTreeItemAttach);
	if (pGameListItem->m_hTreeItemNormal!=NULL) ExpandListItem(pGameListItem->m_hTreeItemNormal);
	
	return true;
}




//绘画图标
VOID CServerListView::DrawItem(CDC * pDC, CRect rcItem, HTREEITEM hTreeItem, bool bHovered, bool bSelected)
{


	return;
}

//绘画图标
VOID CServerListView::DrawListImage(CDC * pDC, CRect rcRect, HTREEITEM hTreeItem, bool bHovered, bool bSelected)
{
	COLORREF crString=RGB(150,150,150);

	//获取属性
	INT nImage,nSelectedImage;
	GetItemImage(hTreeItem,nImage,nSelectedImage);

	//获取信息
	IMAGEINFO ImageInfo;
	m_ImageList.GetImageInfo(nImage,&ImageInfo);

	rcRect.DeflateRect(1, 1);

	UINT uItemState=GetItemState(hTreeItem,TVIF_STATE);


	//绘画图标
	CGameListItem * pGameListItem=(CGameListItem *)GetItemData(hTreeItem);
	if (pGameListItem!=NULL)
	{
		//选择字体
		switch (pGameListItem->GetItemGenre())
		{
		case ItemGenre_Type:		//种类子项
			{
								//设置颜色
				crString=RGB(13,61,88);

				//设置字体
				pDC->SelectObject(m_FontBold);

				int nIndexBkgnd = 0;
				if (bSelected)			nIndexBkgnd = 2;
				else if (bHovered)		nIndexBkgnd = 1;
					
				m_ImageTypeBkgnd.StretchBlt(pDC->GetSafeHdc(), rcRect.left, rcRect.top, rcRect.Width(), rcRect.Height(), 
															   m_ImageTypeBkgnd.GetWidth() / 3 * nIndexBkgnd, 0, m_ImageTypeBkgnd.GetWidth() / 3, m_ImageTypeBkgnd.GetHeight()); 
				CSize SizeArrow(m_ImageArrow1.GetWidth(), m_ImageArrow1.GetHeight());
				INT nIndex=(uItemState&TVIS_EXPANDED)?1L:0L;
				m_ImageArrow1.DrawImage(pDC, rcRect.left + 6, rcRect.top + 8, SizeArrow.cx / 2, SizeArrow.cy, m_ImageArrow1.GetWidth() / 2 * nIndex, 0, SizeArrow.cx / 2, SizeArrow.cy);
				break;
			}
		case ItemGenre_Kind:		// 游戏类型
			{
				//设置颜色
				crString=RGB(13,61,88);

				//设置字体
				pDC->SelectObject(CSkinResourceManager::GetInstance()->GetDefaultFont());

				//计算位置
				INT nXPos=rcRect.left + 11;
				INT nYPos=rcRect.top + 6;

				//绘画图标
				INT nIndex=(uItemState&TVIS_EXPANDED)?1L:0L;
				m_ImageArrow2.DrawImage(pDC,nXPos,nYPos,m_ImageArrow2.GetWidth()/2,m_ImageArrow2.GetHeight(),nIndex*m_ImageArrow2.GetWidth()/2,0);

				m_ImageList.Draw(pDC,nImage, CPoint(nXPos + 11, rcRect.top + 4),ILD_TRANSPARENT);
				break;
			}
		case ItemGenre_Inside:		//内部子项
			{
			//绘制箭头

							//设置颜色
				crString=RGB(0,0,0);

				//设置字体
				CGameInsideItem * pGameInsideItem=(CGameInsideItem *)pGameListItem;
				pDC->SelectObject((pGameInsideItem->m_dwInsideID==0)?m_FontBold:CSkinResourceManager::GetInstance()->GetDefaultFont());

		
				break;
			}
		default:					//其他子项
			{


				//设置颜色
				crString=RGB(0,0,0);

				//设置字体
				pDC->SelectObject(CSkinResourceManager::GetInstance()->GetDefaultFont());
			}

		}
	}

	//设置环境
	pDC->SetBkMode(TRANSPARENT);
	pDC->SetTextColor(crString);

	//绘画字体
	rcRect.right += 22;
	rcRect.left += 42;
	CString strString=GetItemText(hTreeItem);
	pDC->DrawText(strString,rcRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

	return;
}

//绘制文本
VOID CServerListView::DrawItemString(CDC * pDC, CRect rcRect, HTREEITEM hTreeItem, bool bSelected)
{
	CRect rcItem = rcRect;
	//变量定义
	COLORREF crString=RGB(150,150,150);
	CGameListItem * pGameListItem=(CGameListItem *)GetItemData(hTreeItem);

	//颜色定义
	if (pGameListItem!=NULL)
	{
		//选择字体
		switch (pGameListItem->GetItemGenre())
		{
		case ItemGenre_Type:		//种类子项
			{
				//设置颜色
				crString=RGB(13,61,88);

				//设置字体
				pDC->SelectObject(m_FontBold);

				break;
			}
		case ItemGenre_Inside:		//内部子项
			{
				//设置颜色
				crString=RGB(0,0,0);

				//设置字体
				CGameInsideItem * pGameInsideItem=(CGameInsideItem *)pGameListItem;
				pDC->SelectObject((pGameInsideItem->m_dwInsideID==0)?m_FontBold:CSkinResourceManager::GetInstance()->GetDefaultFont());

				break;
			}
		case ItemGenre_Server:
			{
				crString=RGB(51,71,81);
				pDC->SelectObject(m_FontUnderLine); 
				break;
			}
		default:					//其他子项
			{
				//设置颜色
				crString=RGB(13,61,88);

				//设置字体
				pDC->SelectObject(CSkinResourceManager::GetInstance()->GetDefaultFont());
			}
		}
	}
	else
	{
		//设置颜色
		crString=RGB(0,0,0);

		//设置字体
		pDC->SelectObject(CSkinResourceManager::GetInstance()->GetDefaultFont());
	}

	//设置环境
	pDC->SetBkMode(TRANSPARENT);
	pDC->SetTextColor(crString);

	//绘画字体
	rcRect.right += 42;
	rcRect.left += 42;
	CString strString=GetItemText(hTreeItem);
	pDC->DrawText(strString,rcRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

	
	if (pGameListItem->GetItemGenre() == ItemGenre_Server)
	{
		pDC->SelectObject(m_FontNormal);

		crString=RGB(162,68,255); // 满
		crString=RGB(166,137,0);  // 忙
		crString=RGB(189,47,47);  // 挤
		crString=RGB(81,134,28);  // 闲

		pDC->SetTextColor(crString);
		rcRect.right += 102;
		rcRect.left += 102;
		CString strString=GetItemText(hTreeItem);
		pDC->DrawText(TEXT("忙"),rcRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);


		crString=RGB(105,142,160); 

		pDC->SetTextColor(crString);
		pDC->DrawText(TEXT("10萬以上可进  "),rcItem,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);


	}

	return;
}

//获取通知
VOID CServerListView::OnGameItemFinish()
{
	//变量定义
	POSITION Position=NULL;
	CServerListData * pServerListData=CServerListData::GetInstance();

	//展开列表
	for (DWORD i=0;i<pServerListData->GetGameTypeCount();i++)
	{
		CGameTypeItem * pGameTypeItem=pServerListData->EmunGameTypeItem(Position);
		if (pGameTypeItem!=NULL) ExpandListItem(pGameTypeItem);
	}

	//展开列表
//	ExpandListItem(&m_LastServerItem);

	//展开列表
	if (m_TreeListRoot!=NULL) ExpandListItem(m_TreeListRoot);

	//保证可视
	EnsureVisible(m_TreeListRoot);

	//更新人数
	ASSERT(CMissionList::GetInstance()!=NULL);
	CMissionList::GetInstance()->UpdateOnLineInfo();
	SetTimer(IDI_UPDATE_ONLINE,TIME_UPDATE_ONLINE,NULL);

	return;
}

//获取通知
VOID CServerListView::OnGameKindFinish(WORD wKindID)
{
	//获取类型
	CServerListData * pServerListData=CServerListData::GetInstance();
	CGameKindItem * pGameKindItem=pServerListData->SearchGameKind(wKindID);

	//类型处理
	if (pGameKindItem!=NULL)
	{
		//变量定义
		LPCTSTR pszTitle=TEXT("没有可用游戏房间");
		HTREEITEM hItemAttachUpdate=pGameKindItem->m_hItemAttachUpdate;
		HTREEITEM hItemNormalUpdate=pGameKindItem->m_hItemNormalUpdate;

		//更新标题
		if (hItemAttachUpdate!=NULL) SetItemText(hItemAttachUpdate,pszTitle);
		if (hItemNormalUpdate!=NULL) SetItemText(hItemNormalUpdate,pszTitle);
	}

	return;
}

//更新通知
VOID CServerListView::OnGameItemUpdateFinish()
{
	//更新人数
	CPlatformFrame::GetInstance()->UpDataAllOnLineCount();

	return;
}

//插入通知
VOID CServerListView::OnGameItemInsert(CGameListItem * pGameListItem)
{
	//效验参数
	ASSERT(pGameListItem!=NULL);
	if (pGameListItem==NULL) return;

	//插入处理
	switch (pGameListItem->GetItemGenre())
	{
	case ItemGenre_Type:	//游戏种类
		{
			//变量定义
			CGameTypeItem * pGameTypeItem=(CGameTypeItem *)pGameListItem;
			CGameListItem * pParentListItem=pGameListItem->GetParentListItem();

			//属性定义
			LPCTSTR pszTypeName=pGameTypeItem->m_GameType.szTypeName;

			//插入新项
			if (pParentListItem!=NULL)
			{
				//常规插入
				HTREEITEM hTreeItemNormal=pParentListItem->m_hTreeItemNormal;
				if (hTreeItemNormal!=NULL) pGameTypeItem->m_hTreeItemNormal=InsertGameListItem(pszTypeName,IND_TYPE,pGameTypeItem,hTreeItemNormal);

				//喜爱插入
				HTREEITEM hTreeItemAttach=pParentListItem->m_hTreeItemAttach;
				if (hTreeItemAttach!=NULL) pGameTypeItem->m_hTreeItemAttach=InsertGameListItem(pszTypeName,IND_TYPE,pGameTypeItem,hTreeItemAttach);
			}
			else
			{
				//常规插入
				HTREEITEM hTreeItemNormal=m_TreeListRoot;
				pGameTypeItem->m_hTreeItemNormal=InsertGameListItem(pszTypeName,IND_TYPE,pGameTypeItem,hTreeItemNormal);
			}

			//设置状态
			if (pGameTypeItem->m_hTreeItemNormal!=NULL) SetItemState(pGameTypeItem->m_hTreeItemNormal,TVIS_BOLD,TVIS_BOLD);
			if (pGameTypeItem->m_hTreeItemAttach!=NULL) SetItemState(pGameTypeItem->m_hTreeItemAttach,TVIS_BOLD,TVIS_BOLD);

			break;
		}
	case ItemGenre_Kind:	//游戏类型
		{
			//变量定义
			CGameKindItem * pGameKindItem=(CGameKindItem *)pGameListItem;
			CGameListItem * pParentListItem=pGameListItem->GetParentListItem();

			//插入新项
			if (pParentListItem!=NULL)
			{
				//变量定义
				UINT nUpdateImage=IND_SERVER_UPDATE;
				UINT uNormalImage=GetGameImageIndex(pGameKindItem);

				//插入处理
				TCHAR szTitle[64]=TEXT("");
				LPCTSTR pszUpdateName=TEXT("正在下载房间列表...");
				GetGameItemTitle(pGameKindItem,szTitle,CountArray(szTitle));

				//常规插入
				if (pParentListItem->m_hTreeItemNormal!=NULL)
				{
					HTREEITEM hTreeItemNormal=pParentListItem->m_hTreeItemNormal;
					pGameKindItem->m_hTreeItemNormal=InsertGameListItem(szTitle,uNormalImage,pGameKindItem,hTreeItemNormal);
					pGameKindItem->m_hItemNormalUpdate=InsertGameListItem(pszUpdateName,nUpdateImage,NULL,pGameKindItem->m_hTreeItemNormal);
				}

				//喜爱插入
				if (pParentListItem->m_hTreeItemAttach!=NULL)
				{
					HTREEITEM hTreeItemAttach=pParentListItem->m_hTreeItemAttach;
					pGameKindItem->m_hTreeItemAttach=InsertGameListItem(szTitle,uNormalImage,pGameKindItem,hTreeItemAttach);
					pGameKindItem->m_hItemAttachUpdate=InsertGameListItem(pszUpdateName,nUpdateImage,NULL,pGameKindItem->m_hTreeItemAttach);
				}
			}

			break;
		}
	case ItemGenre_Node:	//游戏节点
		{
			//变量定义
			CGameNodeItem * pGameNodeItem=(CGameNodeItem *)pGameListItem;
			CGameListItem * pParentListItem=pGameListItem->GetParentListItem();

			//插入新项
			if (pParentListItem!=NULL)
			{
				//变量定义
				LPCTSTR pszNodeName=pGameNodeItem->m_GameNode.szNodeName;

				//删除更新
				DeleteUpdateItem(pGameNodeItem->GetParentListItem());

				//常规插入
				HTREEITEM hTreeItemNormal=pParentListItem->m_hTreeItemNormal;
				if (hTreeItemNormal!=NULL) pGameNodeItem->m_hTreeItemNormal=InsertGameListItem(pszNodeName,IND_NODE,pGameNodeItem,hTreeItemNormal);

				//喜爱插入
				HTREEITEM hTreeItemAttach=pParentListItem->m_hTreeItemAttach;
				if (hTreeItemAttach!=NULL) pGameNodeItem->m_hTreeItemAttach=InsertGameListItem(pszNodeName,IND_NODE,pGameNodeItem,hTreeItemAttach);
			}

			break;
		}
	case ItemGenre_Page:	//定制子项
		{
			//变量定义
			CGameListItem * pParentListItem=pGameListItem->GetParentListItem();
			LPCTSTR pszDisplayName=(((CGamePageItem *)pGameListItem)->m_GamePage.szDisplayName);

			//插入新项
			if (pParentListItem!=NULL)
			{
				//常规插入
				HTREEITEM hTreeItemNormal=pParentListItem->m_hTreeItemNormal;
				if (hTreeItemNormal!=NULL) pParentListItem->m_hTreeItemNormal=InsertGameListItem(pszDisplayName,IND_BROWSE,pGameListItem,hTreeItemNormal);

				//喜爱插入
				HTREEITEM hTreeItemAttach=pParentListItem->m_hTreeItemAttach;
				if (hTreeItemAttach!=NULL) pParentListItem->m_hTreeItemAttach=InsertGameListItem(pszDisplayName,IND_BROWSE,pGameListItem,hTreeItemAttach);
			}
			else
			{
				//常规插入
				HTREEITEM hTreeItemNormal=m_TreeListRoot;
				pGameListItem->m_hTreeItemNormal=InsertGameListItem(pszDisplayName,IND_BROWSE,pGameListItem,hTreeItemNormal);
			}

			break;
		}
	case ItemGenre_Server:	//游戏房间
		{
			//变量定义
			CGameListItem * pParentListItem=pGameListItem->GetParentListItem();
			CGameServerItem * pGameServerItem=(CGameServerItem *)pGameListItem;

			//插入新项
			if (pParentListItem!=NULL)
			{
				//插入处理
				TCHAR szTitle[64]=TEXT("");
				UINT uImageIndex=GetGameImageIndex(pGameServerItem);
				GetGameItemTitle(pGameServerItem,szTitle,CountArray(szTitle));

				//删除更新
				DeleteUpdateItem(pGameServerItem->GetParentListItem());

				//常规插入
				HTREEITEM hTreeItemNormal=pParentListItem->m_hTreeItemNormal;
				if (hTreeItemNormal!=NULL) pGameServerItem->m_hTreeItemNormal=InsertGameListItem(szTitle,uImageIndex,pGameServerItem,hTreeItemNormal);

				//喜爱插入
				HTREEITEM hTreeItemAttach=pParentListItem->m_hTreeItemAttach;
				if (hTreeItemAttach!=NULL) pGameServerItem->m_hTreeItemAttach=InsertGameListItem(szTitle,uImageIndex,pGameServerItem,hTreeItemAttach);

				//最近插入
				if(IsLastPlayGame(pGameServerItem->m_GameServer.wServerID))
				{
//					InsertGameListItem(szTitle,uImageIndex,pGameServerItem,m_LastServerItem.m_hTreeItemNormal);
					if (m_pILastGameServerSink)
					{
						m_pILastGameServerSink->OnAddLastPlayGame(pGameServerItem->m_GameServer.wServerID, pGameServerItem);
					}
				}
			}
			break;
		}
	}

	return;
}

//更新通知
VOID CServerListView::OnGameItemUpdate(CGameListItem * pGameListItem)
{
	//效验参数
	ASSERT(pGameListItem!=NULL);
	if (pGameListItem==NULL) return;

	//插入处理
	switch (pGameListItem->GetItemGenre())
	{
	case ItemGenre_Type:	//游戏种类
		{
			//变量定义
			CGameTypeItem * pGameTypeItem=(CGameTypeItem *)pGameListItem;

			//设置子项
			if (pGameTypeItem->m_hTreeItemAttach!=NULL)
			{
				LPCTSTR pszTypeName(pGameTypeItem->m_GameType.szTypeName);
				ModifyGameListItem(pGameTypeItem->m_hTreeItemAttach,pszTypeName,IND_TYPE);
			}

			//设置子项
			if (pGameTypeItem->m_hTreeItemNormal!=NULL)
			{
				LPCTSTR pszTypeName(pGameTypeItem->m_GameType.szTypeName);
				ModifyGameListItem(pGameTypeItem->m_hTreeItemNormal,pszTypeName,IND_TYPE);
			}

			break;
		}
	case ItemGenre_Kind:	//游戏类型
		{
			//变量定义
			CGameKindItem * pGameKindItem=(CGameKindItem *)pGameListItem;

			//构造数据
			TCHAR szTitle[64]=TEXT("");
			UINT uNormalImage=GetGameImageIndex(pGameKindItem);
			GetGameItemTitle(pGameKindItem,szTitle,CountArray(szTitle));

			//设置子项
			if (pGameKindItem->m_hTreeItemAttach!=NULL)
			{
				ModifyGameListItem(pGameKindItem->m_hTreeItemAttach,szTitle,uNormalImage);
			}

			//设置子项
			if (pGameKindItem->m_hTreeItemNormal!=NULL)
			{
				ModifyGameListItem(pGameKindItem->m_hTreeItemNormal,szTitle,uNormalImage);
			}

			break;
		}
	case ItemGenre_Node:	//游戏节点
		{
			//变量定义
			CGameNodeItem * pGameNodeItem=(CGameNodeItem *)pGameListItem;

			//设置子项
			if (pGameNodeItem->m_hTreeItemAttach!=NULL)
			{
				LPCTSTR pszNodeName(pGameNodeItem->m_GameNode.szNodeName);
				ModifyGameListItem(pGameNodeItem->m_hTreeItemAttach,pszNodeName,IND_NODE);
			}

			//设置子项
			if (pGameNodeItem->m_hTreeItemNormal!=NULL)
			{
				LPCTSTR pszNodeName(pGameNodeItem->m_GameNode.szNodeName);
				ModifyGameListItem(pGameNodeItem->m_hTreeItemNormal,pszNodeName,IND_NODE);
			}

			break;
		}
	case ItemGenre_Page:	//定制子项
		{
			//变量定义
			CGamePageItem * pGamePageItem=(CGamePageItem *)pGameListItem;

			//设置子项
			if (pGamePageItem->m_hTreeItemAttach!=NULL)
			{
				LPCTSTR pszDisplayName(pGamePageItem->m_GamePage.szDisplayName);
				ModifyGameListItem(pGamePageItem->m_hTreeItemAttach,pszDisplayName,IND_BROWSE);
			}

			//设置子项
			if (pGamePageItem->m_hTreeItemNormal!=NULL)
			{
				LPCTSTR pszDisplayName(pGamePageItem->m_GamePage.szDisplayName);
				ModifyGameListItem(pGamePageItem->m_hTreeItemNormal,pszDisplayName,IND_BROWSE);
			}

			break;
		}
	case ItemGenre_Server:	//游戏房间
		{
			//变量定义
			CGameServerItem * pGameServerItem=(CGameServerItem *)pGameListItem;

			//构造数据
			TCHAR szTitle[64]=TEXT("");
			UINT uImageIndex=GetGameImageIndex(pGameServerItem);
			GetGameItemTitle(pGameServerItem,szTitle,CountArray(szTitle));

			//设置子项
			if (pGameServerItem->m_hTreeItemAttach!=NULL)
			{
				ModifyGameListItem(pGameServerItem->m_hTreeItemAttach,szTitle,uImageIndex);
			}

			//设置子项
			if (pGameServerItem->m_hTreeItemNormal!=NULL)
			{
				ModifyGameListItem(pGameServerItem->m_hTreeItemNormal,szTitle,uImageIndex);
			}

			//最近插入
			if(IsLastPlayGame(pGameServerItem->m_GameServer.wServerID))
			{
//				HTREEITEM hTreeServerItem = EmunGameServerItem(m_LastServerItem.m_hTreeItemNormal, pGameServerItem->m_GameServer.wServerID);
//				if(hTreeServerItem!=NULL)
//				{
//					ModifyGameListItem(hTreeServerItem,szTitle,uImageIndex);
//				}
				if (m_pILastGameServerSink)
				{
					m_pILastGameServerSink->OnUpdateLastPlayGame(pGameServerItem->m_GameServer.wServerID, pGameServerItem);
				}
			}

			break;
		}
	}

	return;
}

//删除通知
VOID CServerListView::OnGameItemDelete(CGameListItem * pGameListItem)
{
	//效验参数
	ASSERT(pGameListItem!=NULL);
	if (pGameListItem==NULL) return;

	//删除树项
	if (pGameListItem->m_hTreeItemAttach!=NULL) DeleteItem(pGameListItem->m_hTreeItemAttach);
	if (pGameListItem->m_hTreeItemNormal!=NULL) DeleteItem(pGameListItem->m_hTreeItemNormal);

	//删除树项
	if(pGameListItem->GetItemGenre() == ItemGenre_Server)
	{
//		HTREEITEM hTreeitem=EmunGameServerItem(m_LastServerItem.m_hTreeItemNormal,((CGameServerItem *)pGameListItem)->m_GameServer.wServerID);
//		if(hTreeitem!=NULL) DeleteItem(hTreeitem);
		if (m_pILastGameServerSink)
		{
			m_pILastGameServerSink->OnRemoveLastPlayGame(((CGameServerItem *)pGameListItem)->m_GameServer.wServerID);
		}
	}

	return;
}

//VOID CServerListView::OnMoveLastGameServerCtrl(int nWidth, int nHeight)
//{
//	CRect rcClient;
//	GetWindowRect(rcClient);
//	MoveWindow(rcClient.left, rcClient.top, rcClient.Width(), rcClient.Height());
//	return;
//}

//获取图标
UINT CServerListView::GetGameImageIndex(CGameKindItem * pGameKindItem)
{
	//安装判断
	if (pGameKindItem->m_dwProcessVersion==0L) return IND_KIND_NODOWN;

	//寻找现存
	UINT uImageIndxe=0;
	tagGameKind * pGameKind=&pGameKindItem->m_GameKind;
	if (m_GameImageMap.Lookup(pGameKind->wKindID,uImageIndxe)==TRUE) return uImageIndxe;

	//加载图标
	if (pGameKindItem->m_dwProcessVersion!=0L)
	{
		//加载资源
		LPCTSTR strProcessName(pGameKind->szProcessName);
		HINSTANCE hInstance=AfxLoadLibrary(strProcessName);

		//加载图形
		CBitmap GameLogo;
		AfxSetResourceHandle(hInstance);
		if (GameLogo.LoadBitmap(TEXT("GAME_LOGO"))) uImageIndxe=m_ImageList.Add(&GameLogo,RGB(255,0,255));
		AfxSetResourceHandle(GetModuleHandle(NULL));

		//释放资源
		AfxFreeLibrary(hInstance);

		//保存信息
		if (uImageIndxe!=0L)
		{
			m_GameImageMap[pGameKind->wKindID]=uImageIndxe;
			return uImageIndxe;
		}
	}
	
	return IND_KIND_NODOWN;
}

//获取图标
UINT CServerListView::GetGameImageIndex(CGameServerItem * pGameServerItem)
{
	//获取图标
	if (pGameServerItem->m_ServerStatus==ServerStatus_Normal) return IND_SERVER_NORMAL;
	if (pGameServerItem->m_ServerStatus==ServerStatus_Entrance) return IND_SERVER_ENTRANCE;
	if (pGameServerItem->m_ServerStatus==ServerStatus_EverEntrance) return IND_SERVER_EVERENTRANCE;

	return IND_SERVER_NORMAL;
}

//获取标题
LPCTSTR CServerListView::GetGameItemTitle(CGameKindItem * pGameKindItem, LPTSTR pszTitle, UINT uMaxCount)
{
	//变量定义
	LPCTSTR pszKindName(pGameKindItem->m_GameKind.szKindName);
	DWORD dwOnLineCount=pGameKindItem->m_GameKind.dwOnLineCount;
	CParameterGlobal * pParameterGlobal=CParameterGlobal::GetInstance();

	//负载信息
	TCHAR szLoadInfo[32] = {0};
	if(pParameterGlobal->m_bShowServerStatus)
		GetLoadInfoOfServer(&(pGameKindItem->m_GameKind), szLoadInfo, CountArray(szLoadInfo));
	else
		_sntprintf(szLoadInfo,CountArray(szLoadInfo),TEXT("%ld"),dwOnLineCount);

	//构造标题
	_sntprintf(pszTitle,uMaxCount,TEXT("%s"),(LPCTSTR)pszKindName);

	return pszTitle; 
}

//获取标题
LPCTSTR CServerListView::GetGameItemTitle(CGameServerItem * pGameServerItem, LPTSTR pszTitle, UINT uMaxCount)
{
	//变量定义
	LPCTSTR pszServerName(pGameServerItem->m_GameServer.szServerName);
	DWORD dwOnLineCount=pGameServerItem->m_GameServer.dwOnLineCount;
	CParameterGlobal * pParameterGlobal=CParameterGlobal::GetInstance();

	//负载信息
//负载信息
	TCHAR szLoadInfo[32] = {0};
	if(pParameterGlobal->m_bShowServerStatus)
	    GetLoadInfoOfServer(&(pGameServerItem->m_GameServer), szLoadInfo, CountArray(szLoadInfo));
	else

       if(dwOnLineCount > 120)
	{
		_sntprintf(szLoadInfo, CountArray(szLoadInfo), TEXT("满员"),dwOnLineCount);
	}
	else if(dwOnLineCount > 90)
	{
		_sntprintf(szLoadInfo, CountArray(szLoadInfo), TEXT("拥挤"),dwOnLineCount);
	}
	else if(dwOnLineCount > 60)
	{
		_sntprintf(szLoadInfo, CountArray(szLoadInfo), TEXT("火爆"),dwOnLineCount);
	}
	else if(dwOnLineCount > 30)
	{
		_sntprintf(szLoadInfo, CountArray(szLoadInfo), TEXT("良好"),dwOnLineCount);
	}
	else
	{
		_sntprintf(szLoadInfo, CountArray(szLoadInfo), TEXT("空闲"),dwOnLineCount);
	}

	//构造标题
	_sntprintf(pszTitle,uMaxCount,TEXT("%s"),pszServerName,szLoadInfo);

	return pszTitle; 
}

//删除更新
VOID CServerListView::DeleteUpdateItem(CGameListItem * pGameListItem)
{
	//效验参数
	ASSERT(pGameListItem!=NULL);
	if (pGameListItem==NULL) return;

	//删除更新
	while (pGameListItem!=NULL)
	{
		//类型判断
		if (pGameListItem->GetItemGenre()==ItemGenre_Kind)
		{
			//删除子项
			CGameKindItem * pGameKindItem=(CGameKindItem *)pGameListItem;
			if (pGameKindItem->m_hItemAttachUpdate!=NULL) DeleteItem(pGameKindItem->m_hItemAttachUpdate);
			if (pGameKindItem->m_hItemNormalUpdate!=NULL) DeleteItem(pGameKindItem->m_hItemNormalUpdate);

			//设置变量
			pGameKindItem->m_hItemAttachUpdate=NULL;
			pGameKindItem->m_hItemNormalUpdate=NULL;

			break;
		}

		//获取父项
		pGameListItem=pGameListItem->GetParentListItem();
	}

	return;
}

//修改子项
VOID CServerListView::ModifyGameListItem(HTREEITEM hTreeItem, LPCTSTR pszTitle, UINT uImage)
{
	//变量定义
	TVITEM TVItem;
	ZeroMemory(&TVItem,sizeof(TVItem));

	//子项属性
	TVItem.hItem=hTreeItem;
	TVItem.cchTextMax=64;
	TVItem.iImage=uImage;
	TVItem.iSelectedImage=uImage;
	TVItem.pszText=(LPTSTR)pszTitle;
	TVItem.mask=TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_TEXT;

	//修改子项
	SetItem(&TVItem);

	return;
}

//插入子项
HTREEITEM CServerListView::InsertInsideItem(LPCTSTR pszTitle, UINT uImage, DWORD dwInsideID, HTREEITEM hParentItem)
{
	//变量定义
	TV_INSERTSTRUCT InsertStruct;
	ZeroMemory(&InsertStruct,sizeof(InsertStruct));

	//创建变量
	CGameInsideItem * pGameInsideItem=new CGameInsideItem;
	if (pGameInsideItem!=NULL) pGameInsideItem->m_dwInsideID=dwInsideID;

	//设置变量
	InsertStruct.hParent=hParentItem;
	InsertStruct.hInsertAfter=TVI_LAST;

	//子项属性
	InsertStruct.item.cchTextMax=64;
	InsertStruct.item.iImage=uImage;
	InsertStruct.item.iSelectedImage=uImage;
	InsertStruct.item.pszText=(LPTSTR)pszTitle;
	InsertStruct.item.lParam=(LPARAM)pGameInsideItem;
	InsertStruct.item.mask=TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_TEXT|TVIF_PARAM;

	return InsertItem(&InsertStruct);
}

//插入子项
HTREEITEM CServerListView::InsertGameListItem(LPCTSTR pszTitle, UINT uImage, CGameListItem * pGameListItem, HTREEITEM hParentItem)
{
	TRACE(pszTitle);
	//变量定义
	TV_INSERTSTRUCT InsertStruct;
	ZeroMemory(&InsertStruct,sizeof(InsertStruct));

	//设置变量
	InsertStruct.hParent=hParentItem;
	InsertStruct.hInsertAfter=TVI_FIRST;
	InsertStruct.item.cchTextMax=64;
	InsertStruct.item.iImage=uImage;
	InsertStruct.item.iSelectedImage=uImage;
	InsertStruct.item.pszText=(LPTSTR)pszTitle;
	InsertStruct.item.lParam=(LPARAM)pGameListItem;
	InsertStruct.item.mask=TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_TEXT|TVIF_PARAM;

	//寻找子项
	if ((pGameListItem!=NULL))
	{
		//变量定义
		WORD wSortID= pGameListItem->GetSortID();
		HTREEITEM hTreeItem=GetChildItem(hParentItem);

		//枚举子项
		while (hTreeItem!=NULL)
		{
			//获取数据
			CGameListItem * pGameListTemp=(CGameListItem *)GetItemData(hTreeItem);
//			CString str = GetItemText(hTreeItem);

			//数据对比
// 			if ((pGameListTemp!=NULL)&&(pGameListTemp->GetSortID()>wSortID))
// 				break;

			// 有限按照SortID排序,SortID大的排前面
			if ((pGameListTemp!=NULL)&&(pGameListTemp->GetSortID()<wSortID))
				break;

			//设置结果
			InsertStruct.hInsertAfter=hTreeItem;

			//切换树项
			hTreeItem=GetNextItem(hTreeItem,TVGN_NEXT);
		} 
	}

	return InsertItem(&InsertStruct);
}

//重画消息
VOID CServerListView::OnPaint()
{
	CPaintDC dc(this);

	//剪切位置
	CRect rcClip;
	dc.GetClipBox(&rcClip);

	//获取位置
	CRect rcClient;
	GetClientRect(&rcClient);

	//创建缓冲
	CDC BufferDC;
	CBitmap BufferImage;
	BufferDC.CreateCompatibleDC(&dc);
	BufferImage.CreateCompatibleBitmap(&dc,rcClient.Width(),rcClient.Height());

	//设置 DC
	BufferDC.SelectObject(&BufferImage);

	//绘画控件
//	DrawTreeBack(&BufferDC,rcClient,rcClip);
//	DrawTreeItem(&BufferDC,rcClient,rcClip);

	//绘制树
	DrawTree(&BufferDC,rcClient,rcClip);

	//绘画背景
	dc.BitBlt(rcClip.left,rcClip.top,rcClip.Width(),rcClip.Height(),&BufferDC,rcClip.left,rcClip.top,SRCCOPY);

	//删除资源
	BufferDC.DeleteDC();
	BufferImage.DeleteObject();

	return;
}

//绘画背景
VOID CServerListView::DrawTree(CDC * pDC, CRect & rcClient, CRect & rcClipBox)
{
	//绘画背景
	pDC->FillSolidRect(&rcClient,RGB(234,242,247));

	//首项判断
	HTREEITEM hItemCurrent=GetFirstVisibleItem();
	if (hItemCurrent==NULL) return;

	//获取属性
	UINT uTreeStyte=GetWindowLong(m_hWnd,GWL_STYLE);

	//获取对象
	ASSERT(CSkinRenderManager::GetInstance()!=NULL);
	CSkinRenderManager * pSkinRenderManager=CSkinRenderManager::GetInstance();

	//绘画子项
	do
	{
		//变量定义
		CRect rcItem;
		CRect rcRect;

		//获取状态
		HTREEITEM hParent=GetParentItem(hItemCurrent);
		UINT uItemState=GetItemState(hItemCurrent,TVIF_STATE);

		//获取属性
		bool bDrawChildren=(ItemHasChildren(hItemCurrent)==TRUE);
		bool bDrawSelected=(uItemState&TVIS_SELECTED)&&((this==GetFocus())||(uTreeStyte&TVS_SHOWSELALWAYS));
		bool bDrawHovered=(m_hItemMouseHover==hItemCurrent)&&(m_hItemMouseHover!=NULL);

		//获取区域
		if (GetItemRect(hItemCurrent,rcItem,FALSE))
		{
			//绘画过滤
			if (rcItem.top>=rcClient.bottom) break;
			if (rcItem.top>=rcClipBox.bottom) continue;

//			DrawItem(pDC,rcItem,hItemCurrent,m_hItemMouseHover==hItemCurrent,bDrawSelected);	
//			DrawItemString(pDC,rcItem,hItemCurrent,bDrawSelected);

			//获取属性
			INT nImage,nSelectedImage;
			GetItemImage(hItemCurrent,nImage,nSelectedImage);

			//获取信息
			IMAGEINFO ImageInfo;
			m_ImageList.GetImageInfo(nImage,&ImageInfo);

			//绘画图标
			CGameListItem * pGameListItem=(CGameListItem *)GetItemData(hItemCurrent);
			if (pGameListItem!=NULL)
			{
				//选择字体
				switch (pGameListItem->GetItemGenre())
				{
				case ItemGenre_Type:		//种类子项
					{
						CGameTypeItem* pGameTypeItem = (CGameTypeItem*)pGameListItem;
						int nIndexBkgnd = 0;
						if (bDrawSelected)			nIndexBkgnd = 1;
						else if (bDrawHovered)		nIndexBkgnd = 2;
							
						//复制底板
						m_ImageTypeBkgnd.StretchBlt(pDC->GetSafeHdc(), rcItem.left + 1, rcItem.top + 1, rcItem.Width() - 2, rcItem.Height() - 1, 
																	m_ImageTypeBkgnd.GetWidth() / 3 * nIndexBkgnd, 0, m_ImageTypeBkgnd.GetWidth() / 3, m_ImageTypeBkgnd.GetHeight()); 

						//绘制箭头
						CSize SizeArrow(m_ImageArrow1.GetWidth(), m_ImageArrow1.GetHeight());
						m_ImageArrow1.DrawImage(pDC, rcItem.left + 6, rcItem.top + 8, SizeArrow.cx / 2, SizeArrow.cy, m_ImageArrow1.GetWidth() / 2 * ((uItemState&TVIS_EXPANDED)?1L:0L), 0, SizeArrow.cx / 2, SizeArrow.cy);

						//设置环境
						pDC->SelectObject(m_FontBold);
						pDC->SetBkMode(TRANSPARENT);
						pDC->SetTextColor(RGB(13,61,88));

						//绘画字体
						CRect rcText = rcItem;
						rcText.left += 19;  		rcText.top += 3;
						pDC->DrawText(GetItemText(hItemCurrent),rcText,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

						// 绘制游戏数量
						DWORD dwGameKindCount = CServerListData::GetInstance()->GetGameKindCount(pGameTypeItem->m_GameType.wTypeID);
						if (dwGameKindCount>0)
						{
							TCHAR szKindCount[MAX_PATH] = _T("");
							_sntprintf(szKindCount, sizeof(szKindCount), _T("总共%d款"), dwGameKindCount);
							rcText.left += 156;
							pDC->DrawText(szKindCount,rcText,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
						}
						break;
					}
				case ItemGenre_Kind:		//游戏类型
					{
						CGameKindItem* pKindItem = (CGameKindItem*)pGameListItem;
						tagGameKind * pGameKind=&pKindItem->m_GameKind;

						//绘画箭头及游戏图标
						m_ImageArrow2.DrawImage(pDC,rcItem.left + 6,rcItem.top + 8,m_ImageArrow2.GetWidth()/2,m_ImageArrow2.GetHeight(),((uItemState&TVIS_EXPANDED)?1L:0L)*m_ImageArrow2.GetWidth()/2,0);
						m_ImageList.Draw(pDC,nImage, CPoint(rcItem.left + 16,rcItem.top + 5),ILD_TRANSPARENT);

						//绘制底部的装饰线
						m_ImageItemLine.DrawImage(pDC, rcItem.left + 28, rcItem.bottom - m_ImageItemLine.GetHeight());

						//绘制热度标志
						WORD wHotLevel = HOTLEVEL(pGameKind->wSortID);
						m_ImageHotLevel.DrawImage(pDC, rcItem.left + 124, rcItem.top + 5, m_ImageHotLevel.GetWidth() / 2, m_ImageHotLevel.GetHeight(), m_ImageHotLevel.GetWidth() / 2 * wHotLevel, 0, m_ImageHotLevel.GetWidth() / 2, m_ImageHotLevel.GetHeight()); 

						//绘制推荐星级标志
						for (int i = 0; i < STARLEVEL(pGameKind->wSortID); i++)
							m_ImageStarLevel.DrawImage(pDC, rcItem.left + 175 + (m_ImageStarLevel.GetWidth() + 1) * i, rcItem.top + 6); 

						//设置环境
						pDC->SelectObject(CSkinResourceManager::GetInstance()->GetDefaultFont());
						pDC->SetBkMode(TRANSPARENT);
						pDC->SetTextColor(RGB(13,61,88));

						//绘画字体
						CRect rcText = rcItem;
						rcText.left += 35;  		rcText.top += 3;
						pDC->DrawText(GetItemText(hItemCurrent),rcText,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
						break;
					}
				case ItemGenre_Server:		//房间类型
					{
						CGameServerItem* pServerItem = (CGameServerItem*)pGameListItem;
						tagGameServer * pGameServer=&pServerItem->m_GameServer;

						CRect rcServer = rcItem;
						rcServer.DeflateRect(1, 1); 
						if (bDrawHovered)
						{
							pDC->FillRect(rcServer, &CBrush(RGB(204,224,237))); 
						}

						//标志
						int nOverLoadIndex = (int)GetLoadInfoOfServer(pGameServer);

						/*    			case 3: crString=RGB(162,68,255);  strOverLoad = TEXT("繁忙"); break;		
						case 2: crString=RGB(189,47,47);   strOverLoad = TEXT("拥挤"); break;		
						case 1:	crString=RGB(166,137,0);   strOverLoad = TEXT("良好"); break;		
						case 0:	crString=RGB(81,134,28);   strOverLoad = TEXT("空闲"); break;		*/

						if (nOverLoadIndex == 1)
							nOverLoadIndex = 0;
						if (nOverLoadIndex == 2)
							nOverLoadIndex = 3;

						m_ImageVolumnFlag.DrawImage(pDC, rcItem.left + 25, rcItem.top + 9, m_ImageVolumnFlag.GetWidth() / 4, m_ImageVolumnFlag.GetHeight(), m_ImageVolumnFlag.GetWidth()/4*nOverLoadIndex, 0, m_ImageVolumnFlag.GetWidth()/4, m_ImageVolumnFlag.GetHeight()); 

						//绘制房间名字
						pDC->SelectObject(m_FontUnderLine);
						pDC->SetBkMode(TRANSPARENT);
						pDC->SetTextColor(RGB(13,61,88));

						CString strName, strDesc;
						int nIndex=GetItemText(hItemCurrent).Find(TEXT("|"));
						if(nIndex!=-1)
						{
							strName=GetItemText(hItemCurrent).Left(nIndex);
							strDesc=GetItemText(hItemCurrent).Right(GetItemText(hItemCurrent).GetLength()-nIndex-1);
						}
						else
						{
							strName=GetItemText(hItemCurrent);
						}

						CRect rcText = rcItem;
						rcText.left += 37;  		rcText.top += 1;
						pDC->DrawText(strName,rcText,DT_LEFT|DT_VCENTER|DT_SINGLELINE);


						//恢复服务器负载文字
						COLORREF crString=RGB(51,71,81);
						pDC->SelectObject(m_FontNormal);
						CString  strOverLoad;
						switch (nOverLoadIndex)
						{
		/*    			case 3: crString=RGB(162,68,255);  strOverLoad = TEXT("繁忙"); break;		
						case 2: crString=RGB(189,47,47);   strOverLoad = TEXT("拥挤"); break;		
						case 1:	crString=RGB(166,137,0);   strOverLoad = TEXT("良好"); break;		
						case 0:	crString=RGB(81,134,28);   strOverLoad = TEXT("空闲"); break;		*/
						case 3: crString=RGB(162,68,255);  strOverLoad = TEXT("忙碌"); break;		
						case 2: crString=RGB(162,68,255);   strOverLoad = TEXT("忙碌"); break;		
						case 1:	crString=RGB(81,134,28);   strOverLoad = TEXT("良好"); break;		
						case 0:	crString=RGB(81,134,28);   strOverLoad = TEXT("良好"); break;		

						default: break;
						}
						rcText = rcItem;
						pDC->SetTextColor(crString);
						rcText.left += 124;		 rcText.top += 1;
						CString strString=GetItemText(hItemCurrent);
						pDC->DrawText(strOverLoad,rcText,DT_LEFT|DT_VCENTER|DT_SINGLELINE);


						//绘制房间说明
						rcText = rcItem;
						rcText.right -= 28;
						crString=RGB(105,142,160); 
						pDC->SetTextColor(crString);
						pDC->DrawText(strDesc,rcText,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
						break;
					}
				case ItemGenre_Inside:		//内部子项
					{
						//设置颜色
						COLORREF crString=RGB(0,0,0);

						//设置字体
						CGameInsideItem * pGameInsideItem=(CGameInsideItem *)pGameListItem;
						pDC->SelectObject((pGameInsideItem->m_dwInsideID==0)?m_FontBold:CSkinResourceManager::GetInstance()->GetDefaultFont());
						break;
					}
				default:					//其他子项
					{
						//设置颜色
						COLORREF crString=RGB(51,71,81);

						//设置字体
						pDC->SelectObject(CSkinResourceManager::GetInstance()->GetDefaultFont());
						break;
					}

				}
			}
		}
	} while ((hItemCurrent=GetNextVisibleItem(hItemCurrent))!= NULL);

	return;
}

//时间消息
VOID CServerListView::OnTimer(UINT nIDEvent)
{
	//更新人数
	if (nIDEvent==IDI_UPDATE_ONLINE)
	{
		ASSERT(CMissionList::GetInstance()!=NULL);
		CMissionList::GetInstance()->UpdateOnLineInfo();

		return;
	}

	__super::OnTimer(nIDEvent);
}

//绘画背景
BOOL CServerListView::OnEraseBkgnd(CDC * pDC)
{
	return TRUE;
}

//位置消息
VOID CServerListView::OnSize(UINT nType, INT cx, INT cy)
{
	__super::OnSize(nType, cx, cy);

	//获取大小
	CRect rcClient;
	GetClientRect(&rcClient);

	//获取信息
	SCROLLINFO ScrollInfoH;
	SCROLLINFO ScrollInfoV;
	ZeroMemory(&ScrollInfoH,sizeof(ScrollInfoH));
	ZeroMemory(&ScrollInfoV,sizeof(ScrollInfoV));

	//获取信息
	GetScrollInfo(SB_HORZ,&ScrollInfoH,SIF_POS|SIF_RANGE);
	GetScrollInfo(SB_VERT,&ScrollInfoV,SIF_POS|SIF_RANGE);

	//设置变量
	m_nXScroll=-ScrollInfoH.nPos;
	m_nYScroll=-ScrollInfoV.nPos;

	return;
}

//光标消息
BOOL CServerListView::OnSetCursor(CWnd * pWnd, UINT nHitTest, UINT uMessage)
{
	//获取光标
	CPoint MousePoint;
	GetCursorPos(&MousePoint);
	ScreenToClient(&MousePoint);

	//子项测试
	HTREEITEM hItemMouseHover=HitTest(MousePoint);

	//重画判断
	if ((hItemMouseHover!=NULL)&&(hItemMouseHover!=m_hItemMouseHover))
	{
		//设置变量
		m_hItemMouseHover=hItemMouseHover;

		//重画界面
		Invalidate(FALSE);
	}

	//设置光标
	if (hItemMouseHover!=NULL)
	{
		SetCursor(LoadCursor(AfxGetInstanceHandle(),MAKEINTRESOURCE(IDC_HAND_CUR)));
		return true;
	}

	return __super::OnSetCursor(pWnd,nHitTest,uMessage);
}

//右键列表
VOID CServerListView::OnNMRClick(NMHDR * pNMHDR, LRESULT * pResult)
{
	return;
	//获取选择
	HTREEITEM hTreeItem=GetCurrentSelectItem(false);

	//选择判断
	if (hTreeItem==NULL) return;

	//设置选择
	Select(hTreeItem,TVGN_CARET);

	//获取数据
/*	CGameListItem * pGameListItem=(CGameListItem *)GetItemData(hTreeItem);
	if (pGameListItem!=NULL)
	{
		switch (pGameListItem->GetItemGenre())
		{
		case ItemGenre_Kind:	//游戏种类
			{
				//变量定义
				TCHAR szBuffer[64]=TEXT("");
				CGameKindItem * pGameKindItem=(CGameKindItem *)pGameListItem;

				//构造菜单
				CSkinMenu Menu;
				Menu.CreateMenu();

				//自动进入
				Menu.AppendMenu(IDM_ENTER_SERVER,TEXT("自动进入"));
				Menu.AppendSeparator();

				//下载游戏
				CW2CT pszKindName(pGameKindItem->m_GameKind.szKindName);
				_sntprintf(szBuffer,CountArray(szBuffer),TEXT("下载“%s”"),(LPCTSTR)pszKindName);
				Menu.AppendMenu(IDM_DOWN_LOAD_CLIENT,szBuffer);

				//当前版本
				if (pGameKindItem->m_dwProcessVersion!=0)
				{
					DWORD dwVersion=pGameKindItem->m_dwProcessVersion;
					_sntprintf(szBuffer,CountArray(szBuffer),TEXT("安装版本 %d.%d.%d.%d"),GetProductVer(dwVersion),
						GetMainVer(dwVersion),GetSubVer(dwVersion),GetBuildVer(dwVersion));
					Menu.AppendMenu(IDM_NULL_COMMAND,szBuffer,MF_GRAYED);
				}
				else Menu.AppendMenu(IDM_DOWN_LOAD_CLIENT,TEXT("没有安装（点击下载）"));

				//控制菜单
				Menu.AppendSeparator();
				bool bExpand=ExpandVerdict(hTreeItem);
				Menu.AppendMenu(bExpand?IDM_SHRINK_LIST:IDM_EXPAND_LIST,bExpand?TEXT("收缩列表"):TEXT("展开列表"));

				//弹出菜单
				Menu.TrackPopupMenu(this);

				return;
			}
		case ItemGenre_Server:	//游戏房间
			{
				//变量定义
				TCHAR szBuffer[64]=TEXT("");
				CGameServerItem * pGameServerItem=(CGameServerItem *)pGameListItem;
				CGameKindItem * pGameKindItem=pGameServerItem->m_pGameKindItem;

				//构造菜单
				CSkinMenu Menu;
				Menu.CreateMenu();
				Menu.AppendMenu(IDM_ENTER_SERVER,TEXT("进入游戏房间"));
				Menu.AppendSeparator();
				Menu.AppendMenu(IDM_SET_COLLECTION,TEXT("设为常用服务器"));

				//下载游戏
				CW2CT pszKindName(pGameKindItem->m_GameKind.szKindName);
				_sntprintf(szBuffer,CountArray(szBuffer),TEXT("下载“%s”"),(LPCTSTR)pszKindName);
				Menu.AppendMenu(IDM_DOWN_LOAD_CLIENT,szBuffer);
				Menu.AppendSeparator();

				//当前版本
				if (pGameKindItem->m_dwProcessVersion!=0)
				{
					DWORD dwVersion=pGameKindItem->m_dwProcessVersion;
					_sntprintf(szBuffer,CountArray(szBuffer),TEXT("安装版本 %d.%d.%d.%d"),GetProductVer(dwVersion),
						GetMainVer(dwVersion),GetSubVer(dwVersion),GetBuildVer(dwVersion));
					Menu.AppendMenu(IDM_NULL_COMMAND,szBuffer,MF_GRAYED);
				}
				else Menu.AppendMenu(IDM_DOWN_LOAD_CLIENT,TEXT("没有安装（点击下载）"));

				//菜单设置
				bool Collection=false;//pGameServerItem->IsCollection();
				if (Collection==true) Menu.CheckMenuItem(IDM_SET_COLLECTION,MF_BYCOMMAND|MF_CHECKED);

				//弹出菜单
				Menu.TrackPopupMenu(this);

				return;
			}
		}
	}*/

	return;
}

//列表改变
VOID CServerListView::OnTvnSelchanged(NMHDR * pNMHDR, LRESULT * pResult)
{
	TRACE("OnTvnSelchanged\n");

	//获取选择
	HTREEITEM hTreeItem=GetSelectedItem();

	//选择判断
	if (hTreeItem==NULL) return;

	//获取数据
	CGameListItem * pGameListItem=(CGameListItem *)GetItemData(hTreeItem);

	//数据处理
	if (pGameListItem!=NULL)
	{
		switch (pGameListItem->GetItemGenre())
		{
		case ItemGenre_Kind:	//游戏种类
			{
				//变量定义
				WORD wGameID=((CGameKindItem *)pGameListItem)->m_GameKind.wGameID;
				WORD wKindID=((CGameKindItem *)pGameListItem)->m_GameKind.wKindID;

//				LOGI("CServerListView::OnTvnSelchanged, wGameID="<<wGameID<<", wKindID="<<wKindID);
				//构造地址
				//获取配置文件
				TCHAR szPath[MAX_PATH]=TEXT("");
				CWHService::GetWorkDirectory(szPath, sizeof(szPath));
				TCHAR szFileName[MAX_PATH]=TEXT("");
				_sntprintf(szFileName,CountArray(szFileName),TEXT("%s\\local.dat"), szPath);
				//获取地址
				TCHAR szGameRule[MAX_PATH]=TEXT("");
				GetPrivateProfileString(TEXT("HyperLink"),TEXT("GameRule"),TEXT(""), szGameRule, sizeof(szGameRule), szFileName);
				// 替换成php页面
				TCHAR szRuleLink[MAX_PATH]=TEXT("");
//				_sntprintf(szRuleLink,CountArray(szRuleLink),TEXT("%s/GameRule.aspx?GameID=%ld"),szPlatformLink,wGameID);
				_sntprintf(szRuleLink,CountArray(szRuleLink), szGameRule,wGameID);

				//打开页面
				CPlatformFrame * pPlatformFrame=CPlatformFrame::GetInstance();
				if (pPlatformFrame!=NULL) pPlatformFrame->WebBrowse(szRuleLink,false);

				return;
			}
		case ItemGenre_Page:	//定制类型
			{
				//变量定义
				WORD wPageID=((CGamePageItem *)pGameListItem)->m_GamePage.wPageID;

				//构造地址
				TCHAR szPageLink[MAX_PATH]=TEXT("");
//				_sntprintf(szPageLink,CountArray(szPageLink),TEXT("%s/GamePage.aspx?PageID=%ld"),szPlatformLink,wPageID);
				_sntprintf(szPageLink,CountArray(szPageLink),TEXT("%s/GamePage/Page?PageID=%ld"),szPlatformLink,wPageID);

				//打开页面
				CPlatformFrame * pPlatformFrame=CPlatformFrame::GetInstance();
				if (pPlatformFrame!=NULL) pPlatformFrame->WebBrowse(szPageLink,false);

				return;
			}
		}
	}

	return;
}

#if 0
//列表展开
VOID CServerListView::OnTvnItemexpanding(NMHDR * pNMHDR, LRESULT * pResult)
{
	//变量定义
	LPNMTREEVIEW pNMTreeView=reinterpret_cast<LPNMTREEVIEW>(pNMHDR);

	//动作处理
/*	if (pNMTreeView->action==TVE_EXPAND)
	{
		//获取子项
		CGameListItem * pGameListItem=(CGameListItem *)GetItemData(pNMTreeView->itemNew.hItem);

		//子项处理
		if ((pGameListItem!=NULL)&&(pGameListItem->GetItemGenre()==ItemGenre_Kind))
		{
			//变量定义
			DWORD dwNowTime=(DWORD)time(NULL);
			CGameKindItem * pGameKindItem=(CGameKindItem *)pGameListItem;
			bool bTimeOut=(dwNowTime>=(pGameKindItem->m_dwUpdateTime+30L));

			//更新判断
			if ((pGameKindItem->m_bUpdateItem==false)||(bTimeOut==true))
			{
				//设置列表
				pGameKindItem->m_bUpdateItem=true;
				pGameKindItem->m_dwUpdateTime=(DWORD)time(NULL);

				//设置任务
				CMissionList * pMissionList=CMissionList::GetInstance();
				if (pMissionList!=NULL) pMissionList->UpdateServerInfo(pGameKindItem->m_GameKind.wKindID);
			}

			return;
		}
	}*/

	return;
}
#endif

//获得房间负载信息
LPCTSTR CServerListView::GetLoadInfoOfServer(DWORD dwOnLineCount, DWORD dwMaxCount, LPTSTR pszBuffer, WORD wBufferSize)
{
	if(pszBuffer == NULL) return NULL;
	if(dwMaxCount == 0)dwMaxCount = 2;

	DWORD dwPer = (dwOnLineCount*100)/dwMaxCount;
	if(dwPer > 120)
	{
		_sntprintf(pszBuffer, wBufferSize, TEXT("忙碌"));
	}
	else if(dwPer > 90)
	{
		_sntprintf(pszBuffer, wBufferSize, TEXT("忙碌"));
	}
	else if(dwPer > 60)
	{
		_sntprintf(pszBuffer, wBufferSize, TEXT("忙碌"));
	}
	else if(dwPer > 30)
	{
		_sntprintf(pszBuffer, wBufferSize, TEXT("良好"));
	}
	else
	{
		_sntprintf(pszBuffer, wBufferSize, TEXT("良好"));
	}

	return pszBuffer;
}

//获得房间负载信息
LPCTSTR CServerListView::GetLoadInfoOfServer(tagGameServer * pGameServer, LPTSTR pszBuffer, WORD wBufferSize)
{
	if(pGameServer == NULL || pszBuffer == NULL) return NULL;

	return GetLoadInfoOfServer(pGameServer->dwOnLineCount, pGameServer->dwFullCount, pszBuffer, wBufferSize);
}

int CServerListView::GetLoadInfoOfServer(tagGameServer * pGameServer)
{
	if (pGameServer == NULL) return 0;

	DWORD dwMaxCount = pGameServer->dwFullCount;
	if(dwMaxCount == 0) dwMaxCount = 200;

	DWORD dwPer = (pGameServer->dwOnLineCount*100)/dwMaxCount;
	if(dwPer > 100)
		return 3;		//满员
	else if(dwPer > 80)
		return 2;		//拥挤
	else if(dwPer > 60)
		return 1;		//繁忙
	else
		return 0;		//良好空闲

	return 0;
};


//获得房间负载信息
LPCTSTR CServerListView::GetLoadInfoOfServer(tagGameKind * pGameKind, LPTSTR pszBuffer, WORD wBufferSize)
{
	if(pGameKind == NULL || pszBuffer == NULL) return NULL;

	return GetLoadInfoOfServer(pGameKind->dwOnLineCount, pGameKind->dwFullCount, pszBuffer, wBufferSize);
}

//加载记录
void CServerListView::LoadLastPlayGame()
{
	//工作目录
	TCHAR szDirectory[MAX_PATH]=TEXT("");
	CWHService::GetWorkDirectory(szDirectory,CountArray(szDirectory));

	//构造路径
	TCHAR szFileName[MAX_PATH]={0};
	_sntprintf(szFileName,CountArray(szFileName),TEXT("%s\\LastGame.lst"),szDirectory);

	//读取文件
	CFile file;
	if(file.Open(szFileName, CFile::modeRead))
	{
		//读取数据
		char buffer[128]={0};
		UINT uReadCount=file.Read(buffer, CountArray(buffer));
		uReadCount /= 2;

		//加入记录
		WORD *pServerIDArry = (WORD *)buffer;
		for(BYTE i=0; i<uReadCount; i++)
		{
			if(pServerIDArry[i]>0) m_LastPlayGameList.AddHead(pServerIDArry[i]);
		}

		//关闭文件
		file.Close();
	}

	return;
}

//保存记录
void CServerListView::SaveLastPlayGame()
{
	//工作目录
	TCHAR szDirectory[MAX_PATH]=TEXT("");
	CWHService::GetWorkDirectory(szDirectory,CountArray(szDirectory));

	//构造路径
	TCHAR szFileName[MAX_PATH]={0};
	_sntprintf(szFileName,CountArray(szFileName),TEXT("%s\\LastGame.lst"),szDirectory);

	//写入文件
	CFile file;
	if(file.Open(szFileName, CFile::modeCreate|CFile::modeWrite))
	{
		//设置数据
		POSITION Position=m_LastPlayGameList.GetHeadPosition();
		WORD wServerIDArry[SHOW_LAST_COUNT]={0};
		for(BYTE i=0; i<SHOW_LAST_COUNT; i++)
		{
			if(Position == NULL) break;

			wServerIDArry[i]=m_LastPlayGameList.GetNext(Position);
		}

		//写入数据
		file.Write(wServerIDArry, sizeof(wServerIDArry));

		//关闭文件
		file.Close();
	}

	return;
}


void CServerListView::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	// TODO: 在此添加消息处理程序代码和/或调用默认值
	//__super::OnLButtonDblClk(nFlags, point);
}


//左击列表A
VOID CServerListView::OnNMLClick(NMHDR * pNMHDR, LRESULT * pResult)
{
	return;
}

void CServerListView::OnLButtonDown(UINT nFlags, CPoint point)
{
	__super::OnLButtonDown(nFlags, point); 

	UINT uFlags = 0;

	DWORD dwPos = GetMessagePos();  
    POINT pt;  
    pt.x = LOWORD(dwPos);  
    pt.y = HIWORD(dwPos);
	
	HTREEITEM hItemCurrent = HitTest(point, &uFlags);

	//选择判断
	if (hItemCurrent==NULL) return;

	//设置选择
	Select(hItemCurrent,TVGN_CARET);

	//获取数据
	CGameListItem * pGameListItem=(CGameListItem *)GetItemData(hItemCurrent);

	//种类节点，实现单击
	if ((pGameListItem!=NULL)&&(pGameListItem->GetItemGenre()==ItemGenre_Type||pGameListItem->GetItemGenre()==ItemGenre_Kind)/*&&(((uFlags&TVHT_ONITEMLABEL)!=0)||(uFlags&TVHT_ONITEMRIGHT)!=0)*/)
	{
		UINT uItemState=GetItemState(hItemCurrent,TVIF_STATE);

		if (uFlags&TVHT_ONITEMBUTTON)
			return;
		
		if(uItemState&TVIS_EXPANDED)
			Expand(hItemCurrent, TVE_COLLAPSE);
		else
			Expand(hItemCurrent, TVE_EXPAND);

		return;
	}


	//进入房间
	if ((pGameListItem!=NULL)&&(pGameListItem->GetItemGenre()==ItemGenre_Server))
	{
		//进入房间
		CPlatformFrame * pPlatformFrame=CPlatformFrame::GetInstance();
		pPlatformFrame->EntranceServerItem((CGameServerItem *)pGameListItem);

		return;
	}

	//内部子项
	if ((pGameListItem!=NULL)&&(pGameListItem->GetItemGenre()==ItemGenre_Inside))
	{
		//变量定义
		CGameInsideItem * pGameInsideItem=(CGameInsideItem *)pGameListItem;

		//工作目录
		TCHAR szDirectory[MAX_PATH]=TEXT("");
		CWHService::GetWorkDirectory(szDirectory,CountArray(szDirectory));

		//构造路径
		TCHAR szAssistantPath[MAX_PATH]=TEXT("");
		_sntprintf(szAssistantPath,CountArray(szAssistantPath),TEXT("%s\\Collocate\\Collocate.INI"),szDirectory);

		//读取地址
		TCHAR szItemName[128]=TEXT(""),szAssistantLink[128]=TEXT("");
		_sntprintf(szItemName,CountArray(szItemName),TEXT("Assistant%ld"),pGameInsideItem->m_dwInsideID);
		GetPrivateProfileString(szItemName,TEXT("AssistantLink"),TEXT(""),szAssistantLink,CountArray(szAssistantLink),szAssistantPath);

		//连接地址
		if (szAssistantLink[0]!=0)
		{
			CPlatformFrame * pPlatformFrame=CPlatformFrame::GetInstance();
			if (pPlatformFrame!=NULL) pPlatformFrame->WebBrowse(szAssistantLink,false);
		}

		return;
	}


/*	return;

	UINT uFlags = 0;
	HTREEITEM hItemCurrent = HitTest(point, &uFlags);
	if (hItemCurrent == NULL)
		return;

	TRACE(TEXT("Flag = %ld\n"), uFlags);

	//获取数据
	CGameListItem * pGameListItem=(CGameListItem *)GetItemData(hItemCurrent);
	if (pGameListItem == NULL)
		return;

	//种类节点，实现单击
	if ((pGameListItem!=NULL)&&(pGameListItem->GetItemGenre()==ItemGenre_Type||pGameListItem->GetItemGenre()==ItemGenre_Kind)&&((uFlags&TVHT_ONITEMBUTTON)==0))
	{
		UINT uItemState=GetItemState(hItemCurrent,TVIF_STATE);
		
		if(uItemState&TVIS_EXPANDED)
			Expand(hItemCurrent, TVE_COLLAPSE);
		else
			Expand(hItemCurrent, TVE_EXPAND);

		return;
	}

	//设置选择
	Select(hItemCurrent,TVGN_CARET);

	//获取数据
//	CGameListItem * pGameListItem=(CGameListItem *)GetItemData(hItemCurrent);

	//种类节点
	if ((pGameListItem!=NULL)&&(pGameListItem->GetItemGenre()==ItemGenre_Kind))
	{
	//	ASSERT(0);
	}


	//进入房间
	if ((pGameListItem!=NULL)&&(pGameListItem->GetItemGenre()==ItemGenre_Server))
	{
		//进入房间
		CPlatformFrame * pPlatformFrame=CPlatformFrame::GetInstance();
		pPlatformFrame->EntranceServerItem((CGameServerItem *)pGameListItem);

		return;
	}*/
}
