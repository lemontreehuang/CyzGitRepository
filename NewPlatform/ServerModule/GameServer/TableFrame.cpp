#include "StdAfx.h"
#include "TableFrame.h"
#include "StockManager.h"
#include "DataBasePacket.h"
#include "AttemperEngineSink.h"

#include <stdio.h>

//////////////////////////////////////////////////////////////////////////////////

//断线定义
#define IDI_OFF_LINE							(TIME_TABLE_SINK_RANGE+1)			//断线标识
#define MAX_OFF_LINE							3									//断线次数
#define TIME_OFF_LINE							90000L								//断线时间

#define IDI_GAME_TIME							(TIME_TABLE_SINK_RANGE+2)			//一局时间定时器
#define MAX_GAME_TIME							360000L								//一局最长时间	

#define IDI_CUSTOMTABLE_OVERDUE					(TIME_TABLE_SINK_RANGE+3)			// 自建桌子过期检查定时器
#define CUSTOMTABLE_OVERDUE_FREQUENCY			60000L								// 自建桌子过期检查频率	

#define IDI_CHECK_DISMISS_TABLE					(TIME_TABLE_SINK_RANGE+4)			// 解散游戏投票超时时间
#define CHECK_DISMISS_TABLE_TIME				300000L								// 定时器生效时间

//////////////////////////////////////////////////////////////////////////////////

//组件变量
CStockManager						g_StockManager;						//库存管理

//游戏记录
CGameScoreRecordArray				CTableFrame::m_GameScoreRecordBuffer;

//////////////////////////////////////////////////////////////////////////////////

//构造函数
CTableFrame::CTableFrame()
{
	//固有属性
	m_wTableID=0;
	m_wChairCount=0;
	m_cbStartMode=START_MODE_ALL_READY;
	m_wUserCount=0;

	//标志变量
	m_bGameStarted=false;
	m_bDrawStarted=false;
	m_bTableStarted=false;
	m_bReset = true;
	m_bLockedForCustom = false;
	m_bLockedForDismiss = false;
	ZeroMemory(m_bAllowLookon,sizeof(m_bAllowLookon));
	ZeroMemory(m_lFrozenedScore,sizeof(m_lFrozenedScore));

	//游戏变量
	m_lCellScore=0L;
	m_cbGameStatus=GAME_STATUS_FREE;
	m_wDrawCount=0;

	//时间变量
	m_dwDrawStartTime=0L;
	ZeroMemory(&m_SystemTimeStart,sizeof(m_SystemTimeStart));

	//动态属性
	m_dwTableOwnerID=0L;
	ZeroMemory(m_szEnterPassword,sizeof(m_szEnterPassword));

	//断线变量
	ZeroMemory(m_wOffLineCount,sizeof(m_wOffLineCount));
	ZeroMemory(m_dwOffLineTime,sizeof(m_dwOffLineTime));

	//配置信息
	m_pGameParameter=NULL;
	m_pGameServiceAttrib=NULL;
	m_pGameServiceOption=NULL;
//	m_pGameServiceTableOption = NULL;

	//组件接口
	m_pITimerEngine=NULL;
	m_pITableFrameSink=NULL;
	m_pIMainServiceFrame=NULL;
	m_pIAndroidUserManager=NULL;

	//扩张接口
	m_pITableUserAction=NULL;
	m_pITableUserRequest=NULL;
	m_pIMatchTableAction=NULL;

	//数据接口
	m_pIKernelDataBaseEngine=NULL;
	m_pIRecordDataBaseEngine=NULL;

	//比赛接口
	m_pIGameMatchSink=NULL;

	//用户数组
	ZeroMemory(m_TableUserItemArray,sizeof(m_TableUserItemArray));

	return;
}

//析构函数
CTableFrame::~CTableFrame()
{
	//释放对象
	SafeRelease(m_pITableFrameSink);
	SafeRelease(m_pIMatchTableAction);

	if (m_pIGameMatchSink!=NULL)
	{
		SafeDelete(m_pIGameMatchSink);
	}

	m_mapDismissVote.clear();

	return;
}

// 重置桌子数据
void CTableFrame::Reset()
{
	LOGI("CTableFrame::Reset");
	m_wDrawCount = 0;
	m_bLockedForCustom = false;
	m_bLockedForDismiss = false;
	m_wConcludeType = INVALID_WORD;

	m_pGameTableOption->dwCreatUserID = INVALID_DWORD;
	memset(m_pGameTableOption->szSecret, 0, sizeof(m_pGameTableOption->szSecret));
	m_pGameTableOption->dwRoundCount = 0;						// 创建的局数
	m_pGameTableOption->dwPlayRoundCount = 0;
	::GetSystemTime(&(m_pGameTableOption->TableCreateTime));

	m_mapDismissVote.clear();

	KillGameTimer(IDI_CHECK_DISMISS_TABLE);
	KillGameTimer(IDI_CUSTOMTABLE_OVERDUE);
}

//接口查询
VOID * CTableFrame::QueryInterface(REFGUID Guid, DWORD dwQueryVer)
{
	QUERYINTERFACE(ITableFrame,Guid,dwQueryVer);
	QUERYINTERFACE_IUNKNOWNEX(ITableFrame,Guid,dwQueryVer);
	return NULL;
}

//服务配置
tagGameServiceOption*  CTableFrame::GetGameServiceOption()
{
	return m_pGameServiceOption; 
}

//桌子配置
tagGameTableOption*	CTableFrame::GetGameTableOption()
{
//	return &(m_pGameServiceTableOption->TableOption[m_wTableID]);
	return m_pGameTableOption;
}

//开始游戏
bool CTableFrame::StartGame()
{
	if (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM)
	{
		// 准备解散后就不能开始游戏
		if (m_bLockedForDismiss == true)
		{
			LOGI("CTableFrame::StartGame 游戏准备解散,不能开始游戏");
			return true;
		}
	}
	//游戏状态
	ASSERT(m_bDrawStarted==false);
	if (m_bDrawStarted==true) return false;

	//保存变量
	bool bGameStarted=m_bGameStarted;
	bool bTableStarted=m_bTableStarted;

	//设置状态
	m_bGameStarted=true;
	m_bDrawStarted=true;
	m_bTableStarted=true;

	//开始时间
	GetLocalTime(&m_SystemTimeStart);
	m_dwDrawStartTime=(DWORD)time(NULL);

	//开始设置
	if (bGameStarted==false)
	{
		//状态变量
		ZeroMemory(m_wOffLineCount,sizeof(m_wOffLineCount));
		ZeroMemory(m_dwOffLineTime,sizeof(m_dwOffLineTime));

		//设置用户
		for (WORD i=0;i<m_wChairCount;i++)
		{
			//获取用户
			IServerUserItem * pIServerUserItem=GetTableUserItem(i);

			//设置用户
			if (pIServerUserItem!=NULL)
			{
				//锁定游戏币
				if (m_pGameTableOption->lServiceScore > 0L)
				{
					m_lFrozenedScore[i] = m_pGameTableOption->lServiceScore;
					pIServerUserItem->FrozenedUserScore(m_pGameTableOption->lServiceScore);
				}

				//设置状态
				BYTE cbUserStatus=pIServerUserItem->GetUserStatus();
				if ((cbUserStatus!=US_OFFLINE)&&(cbUserStatus!=US_PLAYING)) pIServerUserItem->SetUserStatus(US_PLAYING,m_wTableID,i);
			}
		}

		//发送状态
		if (bTableStarted!=m_bTableStarted) SendTableStatus();
	}

	//比赛通知
	bool bStart=true;
	if(m_pIGameMatchSink!=NULL) bStart=m_pIGameMatchSink->OnEventGameStart(this, m_wChairCount);

	//通知事件
	ASSERT(m_pITableFrameSink!=NULL);
	if (m_pITableFrameSink!=NULL&&bStart) m_pITableFrameSink->OnEventGameStart();

	// 设置游戏超时定时器,超时自动解散
	if (m_pGameTableOption->nMaxGameTime == 0)
	{
		// 不进行超时解散
	}
	else
	{
		SetGameTimer(IDI_GAME_TIME, m_pGameTableOption->nMaxGameTime * 1000, 1, NULL);
	}

	// 自定义模式,记录开始游戏时的玩家数目
	if (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM)
	{
		for (WORD i = 0; i < GetChairCount(); i++)
		{
			IServerUserItem* pIServerUserItem = GetTableUserItem(i);
			if (pIServerUserItem == nullptr)
			{
				continue;
			}
			DBR_GR_UserStartCustomGame UserStartCustomGame;
			UserStartCustomGame.wServerID = m_pGameServiceOption->wServerID;
			UserStartCustomGame.dwUserID = pIServerUserItem->GetUserID();
			UserStartCustomGame.wTableID = m_wTableID;
			UserStartCustomGame.wChairID = i;
			UserStartCustomGame.dwRoundCount = m_wDrawCount + 1;
			if (m_pIKernelDataBaseEngine)
			{
				m_pIKernelDataBaseEngine->PostDataBaseRequest(DBR_GR_USER_START_CUSTOMGAME, NULL, &UserStartCustomGame, sizeof(DBR_GR_UserStartCustomGame));
			}
		}
	}

	
	return true;
}

//解散游戏
bool CTableFrame::DismissGame()
{
	//状态判断
	ASSERT(m_bTableStarted==true);
	if (m_bTableStarted==false) return false;

	//结束游戏
	if ((m_bGameStarted==true)&&(m_pITableFrameSink->OnEventGameConclude(INVALID_CHAIR,NULL,GER_DISMISS)==false))
	{
		ASSERT(FALSE);
		return false;
	}

	//设置状态
	if ((m_bGameStarted==false)&&(m_bTableStarted==true))
	{
		//设置变量
		m_bTableStarted=false;

		//发送状态
		SendTableStatus();
	}

	return false;
}

//结束游戏
bool CTableFrame::ConcludeGame(BYTE cbGameStatus)
{
	//效验状态
	ASSERT(m_bGameStarted==true);
	if (m_bGameStarted == false)
	{
		LOGI("CTableFrame::ConcludeGame m_bGameStarted == false");
		return false;
	}

	//保存变量
	bool bDrawStarted=m_bDrawStarted;

	//设置状态
	m_bDrawStarted=false;
	m_cbGameStatus=cbGameStatus;
	m_bGameStarted=(cbGameStatus>=GAME_STATUS_PLAY)?true:false;
	m_wDrawCount++;

	// 关闭超时定时器
	KillGameTimer(IDI_GAME_TIME);

	//游戏记录
	RecordGameScore(bDrawStarted);
	
	//结束设置
	if (m_bGameStarted==false)
	{
		//变量定义
		bool bOffLineWait=false;

		//设置用户
		for (WORD i=0;i<m_wChairCount;i++)
		{
			//获取用户
			IServerUserItem * pIServerUserItem=GetTableUserItem(i);

			//用户处理
			if (pIServerUserItem!=NULL)
			{
				tagTimeInfo* TimeInfo=pIServerUserItem->GetTimeInfo();
				//游戏时间
				DWORD dwCurrentTime=(DWORD)time(NULL);
				TimeInfo->dwEndGameTimer=dwCurrentTime;
				//解锁游戏币
				if (m_lFrozenedScore[i]!=0L)
				{
					pIServerUserItem->UnFrozenedUserScore(m_lFrozenedScore[i]);
					m_lFrozenedScore[i]=0L;
				}

				//设置状态
				if (pIServerUserItem->GetUserStatus()==US_OFFLINE)
				{
					//断线处理
					bOffLineWait=true;
					LOGI("CTableFrame::ConcludeGame PerformStandUpAction, (pIServerUserItem->GetUserStatus()==US_OFFLINE), NickName="<<pIServerUserItem->GetNickName());
					if ( (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM) == 0)
					{
						PerformStandUpAction(pIServerUserItem);
					}
				}
				else if (pIServerUserItem->GetUserStatus()==US_TRUSTEE)
				{
					LOGI("CTableFrame::ConcludeGame PerformStandUpAction, (pIServerUserItem->GetUserStatus()==US_TRUSTEE), NickName="<<pIServerUserItem->GetNickName());
					if ((m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM) == 0)
					{
						PerformStandUpAction(pIServerUserItem);
						// 判断玩家是否离开房间了，如果离开了，需要删除玩家
						if (pIServerUserItem->GetBindIndex() == INVALID_WORD)
						{
							pIServerUserItem->SetUserStatus(US_NULL, INVALID_TABLE, INVALID_CHAIR);
						}
					}
				}
				else
				{
					//设置状态
					pIServerUserItem->SetUserStatus(US_SIT,m_wTableID,i);

					//比赛房间就忽略掉
					if(m_pGameServiceOption->wServerType&GAME_GENRE_MATCH)continue;
					//机器处理
					if (pIServerUserItem->IsAndroidUser()==true)
					{
						//查找机器
						CAttemperEngineSink * pAttemperEngineSink=(CAttemperEngineSink *)m_pIMainServiceFrame;
						tagBindParameter * pBindParameter=pAttemperEngineSink->GetBindParameter(pIServerUserItem->GetBindIndex());
						IAndroidUserItem * pIAndroidUserItem=m_pIAndroidUserManager->SearchAndroidUserItem(pIServerUserItem->GetUserID(),
							pBindParameter->dwSocketID);

						//机器处理
						if (pIAndroidUserItem!=NULL)
						{
							//获取时间
							SYSTEMTIME SystemTime;
							GetLocalTime(&SystemTime);
							DWORD dwTimeMask=(1L<<SystemTime.wHour);

							//获取属性
							tagAndroidService * pAndroidService=pIAndroidUserItem->GetAndroidService();
							tagAndroidParameter * pAndroidParameter=pIAndroidUserItem->GetAndroidParameter();

							//设置信息
							if(pAndroidService->dwResidualPlayDraw>0)
								--pAndroidService->dwResidualPlayDraw;

							//服务时间
							if (((pIServerUserItem->GetTableID()==GetTableID())&&(pAndroidParameter->dwServiceTime&dwTimeMask)==0L))
							{
								LOGI("CTableFrame::ConcludeGame PerformStandUpAction, (pAndroidParameter->dwServiceTime&dwTimeMask == 0L), NickName="<<pIServerUserItem->GetNickName());
								PerformStandUpAction(pIServerUserItem);
								continue;
							}

	
							DWORD dwLogonTime=pIServerUserItem->GetLogonTime();
							DWORD dwReposeTime=pAndroidService->dwReposeTime;
							if ((dwLogonTime+dwReposeTime)<dwCurrentTime)
							{
								LOGI("CTableFrame::ConcludeGame PerformStandUpAction, ((dwLogonTime+dwReposeTime)<dwCurrentTime), NickName="<<pIServerUserItem->GetNickName());
								PerformStandUpAction(pIServerUserItem);
								continue;
							}

							//局数判断
							if ((pIServerUserItem->GetTableID()==GetTableID())&&(pAndroidService->dwResidualPlayDraw==0))
							{
								LOGI("CTableFrame::ConcludeGame PerformStandUpAction, (pAndroidService->dwResidualPlayDraw==0), NickName="<<pIServerUserItem->GetNickName());
								PerformStandUpAction(pIServerUserItem);
								continue;
							}
						}
					}
				}
			}
		}

		//删除时间
		if (bOffLineWait==true)
		{
			LOGI("CTableFrame::ConcludeGame, KillGameTimer(IDI_OFF_LINE)");
			KillGameTimer(IDI_OFF_LINE);
		}
	}

	//通知比赛
	if(m_pIGameMatchSink!=NULL) m_pIGameMatchSink->OnEventGameEnd(this,0, NULL, cbGameStatus);

	//重置桌子
	ASSERT(m_pITableFrameSink!=NULL);
	if (m_pITableFrameSink!=NULL) m_pITableFrameSink->RepositionSink();

	//踢出检测
	if (m_bGameStarted==false)
	{
		for (WORD i=0;i<m_wChairCount;i++)
		{
			//获取用户
			if (m_TableUserItemArray[i]==NULL) continue;
			IServerUserItem * pIServerUserItem=m_TableUserItemArray[i];

			//积分限制
			if ( (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM) == 0)
			{
				if ((m_pGameTableOption->lMinTableScore != 0L) && (pIServerUserItem->GetUserScore() < m_pGameTableOption->lMinTableScore))
				{
					//构造提示
					TCHAR szDescribe[128] = TEXT("");
					if (m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)
					{
						_sntprintf(szDescribe, CountArray(szDescribe), TEXT("您的游戏币少于 ") SCORE_STRING TEXT("，不能继续游戏！"), m_pGameTableOption->lMinTableScore);
					}
					else
					{
						_sntprintf(szDescribe, CountArray(szDescribe), TEXT("您的游戏积分少于 ") SCORE_STRING TEXT("，不能继续游戏！"), m_pGameTableOption->lMinTableScore);
					}

					//发送消息(机器人分数小于最低要求后也不退出房间，因为可以取钱继续游戏)
					if (pIServerUserItem->IsAndroidUser() == true)
						SendGameMessage(pIServerUserItem, szDescribe, SMT_CHAT | SMT_CLOSE_GAME | SMT_EJECT);
					else
						SendGameMessage(pIServerUserItem, szDescribe, SMT_CHAT | SMT_CLOSE_GAME | SMT_EJECT);

					//用户起立
					LOGI("CTableFrame::ConcludeGame PerformStandUpAction, " << szDescribe << ", NickName=" << pIServerUserItem->GetNickName());
					PerformStandUpAction(pIServerUserItem);

					continue;
				}
			}
			//关闭判断
			if ((CServerRule::IsForfendGameEnter(m_pGameServiceOption->dwServerRule)==true)&&(pIServerUserItem->GetMasterOrder()==0))
			{
				//发送消息
				LPCTSTR pszMessage=TEXT("由于系统维护，当前游戏桌子禁止用户继续游戏！");
				SendGameMessage(pIServerUserItem,pszMessage,SMT_EJECT|SMT_CHAT|SMT_CLOSE_GAME);

				//用户起立
				LOGI("CTableFrame::ConcludeGame PerformStandUpAction "<<pszMessage<<", NickName="<<pIServerUserItem->GetNickName());
				PerformStandUpAction(pIServerUserItem);

				continue;
			}
		}
	}

	//结束桌子
	if (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM)
	{
		// 所有的局数结束或者游戏提前结束
		if (m_pGameTableOption->dwRoundCount <= m_wDrawCount)
		{
			// 结算桌子
			// 通知游戏服务器解散房间
			DismissCustomTable(CONCLUDE_TYPE_NORMAL);
		}
		if (m_bLockedForDismiss && (m_wConcludeType == CONCLUDE_TYPE_PLAYER_DISMISS || m_wConcludeType == CONCLUDE_TYPE_TIMEOUT_START))
		{
			ConcludeTable();
		}
	}
	else
	{
		ConcludeTable();
	}

	//发送状态
	SendTableStatus();

	return true;
}

//结束桌子
bool CTableFrame::ConcludeTable()
{
	//结束桌子
	if ((m_bGameStarted==false)&&(m_bTableStarted==true))
	{
		//人数判断
		WORD wTableUserCount=GetSitUserCount();
		if (wTableUserCount==0) m_bTableStarted=false;
		if (m_pGameServiceAttrib->wChairCount==MAX_CHAIR) m_bTableStarted=false;

		//模式判断
		if (m_cbStartMode==START_MODE_FULL_READY) m_bTableStarted=false;
		if (m_cbStartMode==START_MODE_PAIR_READY) m_bTableStarted=false;
		if (m_cbStartMode==START_MODE_ALL_READY) m_bTableStarted=false;

		if (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM)
		{
			m_pITableFrameSink->OnEventTableConclude();
		}
	}

	return true;
}

//写入积分
bool CTableFrame::WriteUserScore(WORD wChairID, tagScoreInfo & ScoreInfo, DWORD dwGameMemal, DWORD dwPlayGameTime)
{
	//效验参数
	ASSERT((wChairID<m_wChairCount)&&(ScoreInfo.cbType!=SCORE_TYPE_NULL));
	if ((wChairID>=m_wChairCount)&&(ScoreInfo.cbType==SCORE_TYPE_NULL)) return false;

	//获取用户
	ASSERT(GetTableUserItem(wChairID)!=NULL);
	IServerUserItem * pIServerUserItem=GetTableUserItem(wChairID);
	TCHAR szMessage[128]=TEXT("");

	//写入积分
	if (pIServerUserItem!=NULL)
	{
		//变量定义
		DWORD dwUserMemal=0L;
#ifndef _READ_TABLE_OPTION_
		SCORE lRevenueScore=__min(m_lFrozenedScore[wChairID],m_pGameServiceOption->lServiceScore);
#else
//		SCORE lRevenueScore = __min(m_lFrozenedScore[wChairID], m_pGameServiceOption->TableOption[m_wTableID].lServiceScore);
		SCORE lRevenueScore = __min(m_lFrozenedScore[wChairID], m_pGameTableOption->lServiceScore);
#endif

		//扣服务费
#ifndef _READ_TABLE_OPTION_
		if (m_pGameServiceOption->lServiceScore>0L 
			&& m_pGameServiceOption->wServerType == GAME_GENRE_GOLD
			&& m_pITableFrameSink->QueryBuckleServiceCharge(wChairID))
#else
// 		if (m_pGameServiceOption->TableOption[m_wTableID].lServiceScore > 0L
// 			&& m_pGameServiceOption->wServerType == GAME_GENRE_GOLD
// 			&& m_pITableFrameSink->QueryBuckleServiceCharge(wChairID))
		if (m_pGameTableOption->lServiceScore > 0L
			&& m_pGameServiceOption->wServerType == GAME_GENRE_GOLD
			&& m_pITableFrameSink->QueryBuckleServiceCharge(wChairID))
#endif
		{
			//扣服务费
			ScoreInfo.lScore-=lRevenueScore;
			ScoreInfo.lRevenue+=lRevenueScore;

			//解锁游戏币
			pIServerUserItem->UnFrozenedUserScore(m_lFrozenedScore[wChairID]);
			m_lFrozenedScore[wChairID]=0L;
		}

		//广场赛计分
		m_pIMainServiceFrame->ResultSquareMatchScore(this,wChairID,ScoreInfo);

		//奖牌计算
		if(dwGameMemal != INVALID_DWORD)
		{
			dwUserMemal = dwGameMemal;
		}
		else if (ScoreInfo.lRevenue>0L)
		{
			WORD wMedalRate=m_pGameParameter->wMedalRate;
			dwUserMemal=(DWORD)(ScoreInfo.lRevenue*wMedalRate/1000L);
		}

		//游戏时间
		DWORD dwCurrentTime=(DWORD)time(NULL);
		DWORD dwPlayTimeCount=((m_bDrawStarted==true)?(dwCurrentTime-m_dwDrawStartTime):0L);
		if(dwPlayGameTime!=INVALID_DWORD) dwPlayTimeCount=dwPlayGameTime;

		//变量定义
		tagUserProperty * pUserProperty=pIServerUserItem->GetUserProperty();

		//道具判断
		if(m_pGameServiceOption->wServerType == GAME_GENRE_SCORE)
		{
			if (ScoreInfo.lScore>0L)
			{
				//四倍积分
				if ((pUserProperty->wPropertyUseMark&PT_USE_MARK_FOURE_SCORE)!=0)
				{
					//变量定义
					DWORD dwValidTime=pUserProperty->PropertyInfo[1].wPropertyCount*pUserProperty->PropertyInfo[1].dwValidNum;
					if(pUserProperty->PropertyInfo[1].dwEffectTime+dwValidTime>dwCurrentTime)
					{
						//积分翻倍
						ScoreInfo.lScore *= 4;
						_sntprintf(szMessage,CountArray(szMessage),TEXT("[ %s ] 使用了[ 四倍积分卡 ]，得分翻四倍！)"),pIServerUserItem->GetNickName());
					}
					else
					{
						pUserProperty->wPropertyUseMark&=~PT_USE_MARK_FOURE_SCORE;
					}
				} //双倍积分
				else if ((pUserProperty->wPropertyUseMark&PT_USE_MARK_DOUBLE_SCORE)!=0)
				{
					//变量定义
					DWORD dwValidTime=pUserProperty->PropertyInfo[0].wPropertyCount*pUserProperty->PropertyInfo[0].dwValidNum;
					if (pUserProperty->PropertyInfo[0].dwEffectTime+dwValidTime>dwCurrentTime)
					{
						//积分翻倍
						ScoreInfo.lScore*=2L;
						_sntprintf(szMessage,CountArray(szMessage),TEXT("[ %s ] 使用了[ 双倍积分卡 ]，得分翻倍！"), pIServerUserItem->GetNickName());
					}
					else
					{
						pUserProperty->wPropertyUseMark&=~PT_USE_MARK_DOUBLE_SCORE;
					}
				}
			}
			else
			{
				//附身符
				if ((pUserProperty->wPropertyUseMark&PT_USE_MARK_POSSESS)!=0)
				{
					//变量定义
					DWORD dwValidTime=pUserProperty->PropertyInfo[3].wPropertyCount*pUserProperty->PropertyInfo[3].dwValidNum;
					if(pUserProperty->PropertyInfo[3].dwEffectTime+dwValidTime>dwCurrentTime)
					{
						//积分翻倍
						ScoreInfo.lScore = 0;
						_sntprintf(szMessage,CountArray(szMessage),TEXT("[ %s ] 使用了[ 护身符卡 ]，积分不变！"),pIServerUserItem->GetNickName());
					}
					else
					{
						pUserProperty->wPropertyUseMark &= ~PT_USE_MARK_POSSESS;
					}
				}
			}
		}

		
		// 获取计算钱的总赢量和总输量
		SCORE lTotalWin = pIServerUserItem->GetTotalWin();
		SCORE lTotalLose = pIServerUserItem->GetTotalLose();

		//写入积分
		pIServerUserItem->WriteUserScore(ScoreInfo.lScore,ScoreInfo.lGrade,ScoreInfo.lRevenue,dwUserMemal,ScoreInfo.cbType,dwPlayTimeCount);

		//游戏记录
		if (pIServerUserItem->IsAndroidUser()==false && CServerRule::IsRecordGameScore(m_pGameServiceOption->dwServerRule)==true)
		{
			LOGI("CTableFrame::WriteUserScore 产生玩家游戏记录");
			//变量定义
			tagGameScoreRecord * pGameScoreRecord=NULL;

			//查询库存
			if (m_GameScoreRecordBuffer.GetCount()>0L)
			{
				//获取对象
				INT_PTR nCount=m_GameScoreRecordBuffer.GetCount();
				pGameScoreRecord=m_GameScoreRecordBuffer[nCount-1];

				//删除对象
				m_GameScoreRecordBuffer.RemoveAt(nCount-1);
			}

			//创建对象
			if (pGameScoreRecord==NULL)
			{
				try
				{
					//创建对象
					pGameScoreRecord=new tagGameScoreRecord;
					if (pGameScoreRecord==NULL) throw TEXT("游戏记录对象创建失败");
				}
				catch (...)
				{
					ASSERT(FALSE);
				}
			}

			//记录数据
			if (pGameScoreRecord!=NULL)
			{
				//用户信息
				pGameScoreRecord->wChairID=wChairID;
				pGameScoreRecord->dwUserID=pIServerUserItem->GetUserID();
				pGameScoreRecord->cbAndroid=(pIServerUserItem->IsAndroidUser()?TRUE:FALSE);

				//用户信息
				pGameScoreRecord->dwDBQuestID=pIServerUserItem->GetDBQuestID();
				pGameScoreRecord->dwInoutIndex=pIServerUserItem->GetInoutIndex();

				//游戏币信息
				pGameScoreRecord->lScore=ScoreInfo.lScore;
				pGameScoreRecord->lGrade=ScoreInfo.lGrade;
				pGameScoreRecord->lRevenue=ScoreInfo.lRevenue;

				//附加信息
				pGameScoreRecord->dwUserMemal=dwUserMemal;
				pGameScoreRecord->dwPlayTimeCount=dwPlayTimeCount;

				//机器人免税
				if(pIServerUserItem->IsAndroidUser())
				{
					pGameScoreRecord->lScore += pGameScoreRecord->lRevenue;
					pGameScoreRecord->lRevenue = 0L;
				}

				//插入数据
				m_GameScoreRecordActive.Add(pGameScoreRecord);
			}
		}

		//游戏记录
//		LOGI("CTableFrame::WriteUserScore dwGameMemal="<<dwGameMemal<<", dwPlayTimeCount="<<dwPlayTimeCount);
		if(dwGameMemal != INVALID_DWORD || dwPlayTimeCount!=INVALID_DWORD)
		{
			LOGI("CTableFrame::WriteUserScore 记录游戏记录");
			RecordGameScore(true);
		}
	}
	else
	{
		//离开用户
		CTraceService::TraceString(TEXT("系统暂时未支持离开用户的补分操作处理！"),TraceLevel_Exception);

		return false;
	}

	//广播消息
	if (szMessage[0]!=0)
	{
		//变量定义
		IServerUserItem * pISendUserItem = NULL;
		WORD wEnumIndex=0;

		//游戏玩家
		for (WORD i=0;i<m_wChairCount;i++)
		{
			//获取用户
			pISendUserItem=GetTableUserItem(i);
			if(pISendUserItem==NULL) continue;

			//发送消息
			SendGameMessage(pISendUserItem, szMessage, SMT_CHAT);
		}

		//旁观用户
		do
		{
			pISendUserItem=EnumLookonUserItem(wEnumIndex++);
			if(pISendUserItem!=NULL) 
			{
				//发送消息
				SendGameMessage(pISendUserItem, szMessage, SMT_CHAT);
			}
		} while (pISendUserItem!=NULL);
	}

	return true;
}

//写入积分
bool CTableFrame::WriteTableScore(tagScoreInfo ScoreInfoArray[], WORD wScoreCount)
{
	//效验参数
	ASSERT(wScoreCount==m_wChairCount);
	if (wScoreCount!=m_wChairCount) return false;

	//写入分数
	for (WORD i=0;i<m_wChairCount;i++)
	{
		if (ScoreInfoArray[i].cbType!=SCORE_TYPE_NULL)
		{
			WriteUserScore(i,ScoreInfoArray[i]);
		}
	}

	////广场赛计分
	//m_pIMainServiceFrame->ResultSquareMatchScore(this,ScoreInfoArray,wScoreCount);

	return true;
}

//计算税收
SCORE CTableFrame::CalculateRevenue(WORD wChairID, SCORE lScore)
{
	//效验参数
	ASSERT(wChairID<m_wChairCount);
	if (wChairID>=m_wChairCount) return 0L;

	//计算税收
#ifndef _READ_TABLE_OPTION_
	if ((m_pGameServiceOption->wRevenueRatio>0)&&(lScore>=REVENUE_BENCHMARK))
	{
		//获取用户
		ASSERT(GetTableUserItem(wChairID)!=NULL);
		IServerUserItem * pIServerUserItem=GetTableUserItem(wChairID);

		//计算税收
		SCORE lRevenue=lScore*m_pGameServiceOption->wRevenueRatio/REVENUE_DENOMINATOR;

		return lRevenue;
	}
#else
// 	if ((m_pGameServiceOption->TableOption[m_wTableID].wRevenueRatio>0) && (lScore >= REVENUE_BENCHMARK))
// 	{
// 		//获取用户
// 		ASSERT(GetTableUserItem(wChairID) != NULL);
// 		IServerUserItem * pIServerUserItem = GetTableUserItem(wChairID);
// 
// 		//计算税收
// 		SCORE lRevenue = lScore*m_pGameServiceOption->TableOption[m_wTableID].wRevenueRatio / REVENUE_DENOMINATOR;
// 
// 		return lRevenue;
// 	}
	if ((m_pGameTableOption->wRevenueRatio > 0) && (lScore >= REVENUE_BENCHMARK))
	{
		//获取用户
		ASSERT(GetTableUserItem(wChairID) != NULL);
		IServerUserItem * pIServerUserItem = GetTableUserItem(wChairID);

		//计算税收
		SCORE lRevenue = lScore*m_pGameTableOption->wRevenueRatio / REVENUE_DENOMINATOR;

		return lRevenue;
	}
#endif

	return 0L;
}

//消费限额
SCORE CTableFrame::QueryConsumeQuota(IServerUserItem * pIServerUserItem)
{
	//用户效验
	ASSERT(pIServerUserItem->GetTableID()==m_wTableID);
	if (pIServerUserItem->GetTableID()!=m_wTableID) return 0L;

	//查询额度
	SCORE lTrusteeScore=pIServerUserItem->GetTrusteeScore();
#ifndef _READ_TABLE_OPTION_
	SCORE lMinEnterScore=m_pGameServiceOption->lMinTableScore;
#else
//	SCORE lMinEnterScore = m_pGameServiceOption->TableOption[m_wTableID].lMinTableScore;
	SCORE lMinEnterScore = m_pGameTableOption->lMinTableScore;
#endif
	SCORE lUserConsumeQuota=m_pITableFrameSink->QueryConsumeQuota(pIServerUserItem);

	//效验额度
	ASSERT((lUserConsumeQuota>=0L)&&(lUserConsumeQuota<=pIServerUserItem->GetUserScore()-lMinEnterScore));
	if ((lUserConsumeQuota<0L)||(lUserConsumeQuota>pIServerUserItem->GetUserScore()-lMinEnterScore)) return 0L;

	return lUserConsumeQuota+lTrusteeScore;
}

//寻找用户
IServerUserItem * CTableFrame::SearchUserItem(DWORD dwUserID)
{
	//变量定义
	WORD wEnumIndex=0;
	IServerUserItem * pIServerUserItem=NULL;

	//桌子用户
	for (WORD i=0;i<m_wChairCount;i++)
	{
		pIServerUserItem=GetTableUserItem(i);
		if ((pIServerUserItem!=NULL)&&(pIServerUserItem->GetUserID()==dwUserID)) return pIServerUserItem;
	}

	//旁观用户
	do
	{
		pIServerUserItem=EnumLookonUserItem(wEnumIndex++);
		if ((pIServerUserItem!=NULL)&&(pIServerUserItem->GetUserID()==dwUserID)) return pIServerUserItem;
	} while (pIServerUserItem!=NULL);

	return NULL;
}

//游戏用户
IServerUserItem * CTableFrame::GetTableUserItem(WORD wChairID)
{
	//效验参数
	ASSERT(wChairID<m_wChairCount);
	if (wChairID>=m_wChairCount) return nullptr;

	//获取用户
	return m_TableUserItemArray[wChairID];
}

//旁观用户
IServerUserItem * CTableFrame::EnumLookonUserItem(WORD wEnumIndex)
{
	if (wEnumIndex >= m_LookonUserItemArray.GetCount()) return nullptr;
	return m_LookonUserItemArray[wEnumIndex];
}

//设置时间
bool CTableFrame::SetGameTimer(DWORD dwTimerID, DWORD dwElapse, DWORD dwRepeat, WPARAM dwBindParameter)
{
	LOGI("CTableFrame::SetGameTimer, dwTimerID=" << dwTimerID << ", dwElapse=" << dwElapse << ", dwRepeat=" << dwRepeat);
	//效验参数
	ASSERT((dwTimerID>0)&&(dwTimerID<TIME_TABLE_MODULE_RANGE));
	if ((dwTimerID <= 0) || (dwTimerID >= TIME_TABLE_MODULE_RANGE))
	{
		return false;
	}

	//设置时间
	DWORD dwEngineTimerID=IDI_TABLE_MODULE_START+m_wTableID*TIME_TABLE_MODULE_RANGE;
	if (m_pITimerEngine != NULL)
	{
		LOGI("CTableFrame::SetGameTimer, m_pITimerEngine->SetTimer dwTimerID=" << (dwEngineTimerID + dwTimerID) << "dwElapse=" << dwElapse << ", dwRepeat=" << dwRepeat);
		m_pITimerEngine->SetTimer(dwEngineTimerID + dwTimerID, dwElapse, dwRepeat, dwBindParameter);
	}

	return true;
}

//删除时间
bool CTableFrame::KillGameTimer(DWORD dwTimerID)
{
	LOGI("CTableFrame::KillGameTimer, dwTimerID=" << dwTimerID);
	//效验参数
	ASSERT((dwTimerID>0)&&(dwTimerID<=TIME_TABLE_MODULE_RANGE));
	if ((dwTimerID<=0)||(dwTimerID>TIME_TABLE_MODULE_RANGE)) return false;

	//删除时间
	DWORD dwEngineTimerID=IDI_TABLE_MODULE_START+m_wTableID*TIME_TABLE_MODULE_RANGE;
	if (m_pITimerEngine != NULL)
	{
		LOGI("CTableFrame::KillGameTimer, m_pITimerEngine->KillTimer dwTimerID=" << (dwEngineTimerID + dwTimerID) );
		m_pITimerEngine->KillTimer(dwEngineTimerID + dwTimerID);
	}

	return true;
}

//发送数据
bool CTableFrame::SendUserItemData(IServerUserItem * pIServerUserItem, WORD wSubCmdID)
{
	//状态效验
	ASSERT((pIServerUserItem!=NULL)&&(pIServerUserItem->IsClientReady()==true));
	if ((pIServerUserItem==NULL)&&(pIServerUserItem->IsClientReady()==false)) return false;

	//发送数据
	m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GF_GAME,wSubCmdID,NULL,0);

	return true;
}

//发送数据
bool CTableFrame::SendUserItemData(IServerUserItem * pIServerUserItem, WORD wSubCmdID, VOID * pData, WORD wDataSize)
{
	//状态效验
	ASSERT((pIServerUserItem!=NULL)&&(pIServerUserItem->IsClientReady()==true));
	if ((pIServerUserItem==NULL)&&(pIServerUserItem->IsClientReady()==false)) return false;

	//发送数据
	m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GF_GAME,wSubCmdID,pData,wDataSize);

	return true;
}

//发送数据
bool CTableFrame::SendTableData(WORD wChairID, WORD wSubCmdID)
{
	//用户群发
	if (wChairID==INVALID_CHAIR)
	{
		for (WORD i=0;i<m_wChairCount;i++)
		{
			//获取用户
			IServerUserItem * pIServerUserItem=GetTableUserItem(i);
			if (pIServerUserItem==NULL) continue;

			//效验状态
			ASSERT(pIServerUserItem->IsClientReady()==true);
			if (pIServerUserItem->IsClientReady()==false) continue;

			//发送数据
			m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GF_GAME,wSubCmdID,NULL,0);
		}

		return true;
	}
	else
	{
		//获取用户
		IServerUserItem * pIServerUserItem=GetTableUserItem(wChairID);
		if (pIServerUserItem==NULL) return false;

		//效验状态
		ASSERT(pIServerUserItem->IsClientReady()==true);
		if (pIServerUserItem->IsClientReady()==false) return false;

		//发送数据
		m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GF_GAME,wSubCmdID,NULL,0);

		return true;
	}

	return false;
}

//发送数据
bool CTableFrame::SendTableData(WORD wChairID, WORD wSubCmdID, VOID * pData, WORD wDataSize,WORD wMainCmdID)
{
	//用户群发
	if (wChairID==INVALID_CHAIR)
	{
		for (WORD i=0;i<m_wChairCount;i++)
		{
			//获取用户
			IServerUserItem * pIServerUserItem=GetTableUserItem(i);
			if ((pIServerUserItem==NULL)||(pIServerUserItem->IsClientReady()==false)) continue;

			//发送数据
			m_pIMainServiceFrame->SendData(pIServerUserItem,wMainCmdID,wSubCmdID,pData,wDataSize);
		}

		return true;
	}
	else
	{
		//获取用户
		IServerUserItem * pIServerUserItem=GetTableUserItem(wChairID);
		if ((pIServerUserItem==NULL)||(pIServerUserItem->IsClientReady()==false)) return false;

		//发送数据
		m_pIMainServiceFrame->SendData(pIServerUserItem,wMainCmdID,wSubCmdID,pData,wDataSize);

		return true;
	}

	return false;
}

//发送数据
bool CTableFrame::SendLookonData(WORD wChairID, WORD wSubCmdID)
{
	//变量定义
	WORD wEnumIndex=0;
	IServerUserItem * pIServerUserItem=NULL;

	//枚举用户
	do
	{
		//获取用户
		pIServerUserItem=EnumLookonUserItem(wEnumIndex++);
		if (pIServerUserItem==NULL) break;

		//效验状态
		ASSERT(pIServerUserItem->IsClientReady()==true);
		if (pIServerUserItem->IsClientReady()==false) return false;

		//发送数据
		if ((wChairID==INVALID_CHAIR)||(pIServerUserItem->GetChairID()==wChairID))
		{
			m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GF_GAME,wSubCmdID,NULL,0);
		}

	} while (true);

	return true;
}

//发送数据
bool CTableFrame::SendLookonData(WORD wChairID, WORD wSubCmdID, VOID * pData, WORD wDataSize)
{
	//变量定义
	WORD wEnumIndex=0;
	IServerUserItem * pIServerUserItem=NULL;

	//枚举用户
	do
	{
		//获取用户
		pIServerUserItem=EnumLookonUserItem(wEnumIndex++);
		if (pIServerUserItem==NULL) break;

		//效验状态
		//ASSERT(pIServerUserItem->IsClientReady()==true);
		if (pIServerUserItem->IsClientReady()==false) return false;

		//发送数据
		if ((wChairID==INVALID_CHAIR)||(pIServerUserItem->GetChairID()==wChairID))
		{
			m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GF_GAME,wSubCmdID,pData,wDataSize);
		}

	} while (true);

	return true;
}

//发送消息
bool CTableFrame::SendGameMessage(LPCTSTR lpszMessage, WORD wType)
{
	//变量定义
	WORD wEnumIndex=0;

	//发送消息
	for (WORD i=0;i<m_wChairCount;i++)
	{
		//获取用户
		IServerUserItem * pIServerUserItem=GetTableUserItem(i);
		if ((pIServerUserItem==NULL)||(pIServerUserItem->IsClientReady()==false)) continue;

		//发送消息
		m_pIMainServiceFrame->SendGameMessage(pIServerUserItem,lpszMessage,wType);
	}

	//枚举用户
	do
	{
		//获取用户
		IServerUserItem * pIServerUserItem=EnumLookonUserItem(wEnumIndex++);
		if (pIServerUserItem==NULL) break;

		//效验状态
		ASSERT(pIServerUserItem->IsClientReady()==true);
		if (pIServerUserItem->IsClientReady()==false) return false;

		//发送消息
		m_pIMainServiceFrame->SendGameMessage(pIServerUserItem,lpszMessage,wType);

	} while (true);

	return true;
}

//房间消息
bool CTableFrame::SendRoomMessage(IServerUserItem * pIServerUserItem, LPCTSTR lpszMessage, WORD wType)
{
	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//发送消息
	m_pIMainServiceFrame->SendRoomMessage(pIServerUserItem,lpszMessage,wType);

	return true;
}

//游戏消息
bool CTableFrame::SendGameMessage(IServerUserItem * pIServerUserItem, LPCTSTR lpszMessage, WORD wType)
{
	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//发送消息
	return m_pIMainServiceFrame->SendGameMessage(pIServerUserItem,lpszMessage,wType);
}

//发送系统消息
bool CTableFrame::SendSystemMessage(LPCTSTR lpszMessage, WORD wType)
{
	return m_pIMainServiceFrame->SendSystemMessage(lpszMessage, wType);
}

//发送场景
bool CTableFrame::SendGameScene(IServerUserItem * pIServerUserItem, VOID * pData, WORD wDataSize)
{
	//用户效验
	ASSERT((pIServerUserItem!=NULL)&&(pIServerUserItem->IsClientReady()==true));
	if ((pIServerUserItem==NULL)||(pIServerUserItem->IsClientReady()==false)) return false;

	//发送场景
	ASSERT(m_pIMainServiceFrame!=NULL);
	m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GF_FRAME,SUB_GF_GAME_SCENE,pData,wDataSize);

	return true;
}

//断线事件
bool CTableFrame::OnEventUserOffLine(IServerUserItem * pIServerUserItem)
{
	LOGI("CTableFrame::OnEventUserOffLine PerformStandUpAction1, UserID="<<pIServerUserItem->GetUserID()<<" NickName="<<pIServerUserItem->GetNickName());
	//参数效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//用户属性
	WORD wChairID=pIServerUserItem->GetChairID();
	BYTE cbUserStatus=pIServerUserItem->GetUserStatus();
	LOGI("CTableFrame::OnEventUserOffLine, UserStatus=" << cbUserStatus << ", m_wOffLineCount=" << m_wOffLineCount[wChairID]);
	//游戏用户
	if (cbUserStatus!=US_LOOKON)
	{
		//效验用户
		ASSERT(pIServerUserItem==GetTableUserItem(wChairID));
		if (pIServerUserItem!=GetTableUserItem(wChairID)) return false;
		if(m_pIGameMatchSink)m_pIGameMatchSink->SetUserOffline(wChairID,true);

		//断线处理
		if ((cbUserStatus==US_PLAYING))
		{
			// 自建桌子模式没有托管次数限制
			if (m_wOffLineCount[wChairID]<MAX_OFF_LINE || (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM))
			{
				//用户设置
				pIServerUserItem->SetClientReady(false);
				pIServerUserItem->SetUserStatus(US_OFFLINE,m_wTableID,wChairID);

				//断线处理
				if ((m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM) != 0)
				{
					// 非自建模式才进行断线超时检测,否则一直断线处理
					if (m_dwOffLineTime[wChairID] == 0L)
					{
						//设置变量
						m_wOffLineCount[wChairID]++;
						m_dwOffLineTime[wChairID] = (DWORD)time(NULL);

						//时间设置
						WORD wOffLineCount = GetOffLineUserCount();
						if (wOffLineCount == 1)
						{
							LOGI("CTableFrame::OnEventUserOffLine, wOffLineCount=" << wOffLineCount);
							SetGameTimer(IDI_OFF_LINE, TIME_OFF_LINE, 1, wChairID);
						}
					}
				}
			}

			return true;
		}
	}

	if ((m_pGameServiceOption->wServerType&GAME_GENRE_USERCUSTOM) && m_bTableStarted == true)
	{
	}
	else
	{
		//用户起立
		LOGI("CTableFrame::OnEventUserOffLine PerformStandUpAction2, UserID="<<pIServerUserItem->GetUserID()<<", NickName="<<pIServerUserItem->GetNickName());
		PerformStandUpAction(pIServerUserItem);

		//删除用户
		if ( CServerRule::IsAllowOffLineTrustee(m_pGameServiceOption->dwServerRule) )
		{
			if (pIServerUserItem->GetUserStatus() != US_TRUSTEE)
			{
				ASSERT(pIServerUserItem->GetUserStatus()==US_FREE);
				pIServerUserItem->SetUserStatus(US_NULL,INVALID_TABLE,INVALID_CHAIR);
			}
		}
		else
		{
			ASSERT(pIServerUserItem->GetUserStatus()==US_FREE);
			pIServerUserItem->SetUserStatus(US_NULL,INVALID_TABLE,INVALID_CHAIR);
		}
	}
	return true;
}

//积分事件
bool CTableFrame::OnUserScroeNotify(WORD wChairID, IServerUserItem * pIServerUserItem, BYTE cbReason)
{
	//通知游戏
	return m_pITableFrameSink->OnUserScroeNotify(wChairID,pIServerUserItem,cbReason);
}

//时间事件
bool CTableFrame::OnEventTimer(DWORD dwTimerID, WPARAM dwBindParameter)
{
	//回调事件
	if ((dwTimerID>=0)&&(dwTimerID<TIME_TABLE_SINK_RANGE))
	{
		ASSERT(m_pITableFrameSink!=NULL);
		return m_pITableFrameSink->OnTimerMessage(dwTimerID,dwBindParameter);
	}

	//事件处理
	switch (dwTimerID)
	{
	case IDI_OFF_LINE:	//断线事件
		{
			//效验状态
			ASSERT(m_bGameStarted==true);
			if (m_bGameStarted == false)
			{
				LOGI("CTableFrame::OnEventTimer, m_bGameStarted == false");
				return false;
			}

			//变量定义
			DWORD dwOffLineTime=0L;
			WORD wOffLineChair=INVALID_CHAIR;

			//寻找用户
			for (WORD i=0;i<m_wChairCount;i++)
			{
				if ((m_dwOffLineTime[i]!=0L)&&((m_dwOffLineTime[i]<dwOffLineTime)||(wOffLineChair==INVALID_CHAIR)))
				{
					wOffLineChair=i;
					dwOffLineTime=m_dwOffLineTime[i];
				}
			}

			//位置判断
			ASSERT(wOffLineChair!=INVALID_CHAIR);
			if (wOffLineChair == INVALID_CHAIR)
			{
				LOGI("CTableFrame::OnEventTimer, wOffLineChair == INVALID_CHAIR");
				return false;
			}

			//用户判断
			ASSERT(dwBindParameter<m_wChairCount);
			if (wOffLineChair!=(WORD)dwBindParameter)
			{
				//时间计算
				DWORD dwCurrentTime=(DWORD)time(NULL);
				DWORD dwLapseTime=dwCurrentTime-m_dwOffLineTime[wOffLineChair];

				//设置时间
				dwLapseTime=__min(dwLapseTime,TIME_OFF_LINE-2000L);
				LOGI("CTableFrame::OnEventTimer wOffLineChair!=(WORD)dwBindParameter SetGameTimer time="<<TIME_OFF_LINE-dwLapseTime);
				SetGameTimer(IDI_OFF_LINE,TIME_OFF_LINE-dwLapseTime,1,wOffLineChair);

				return true;
			}

			//获取用户
			ASSERT(GetTableUserItem(wOffLineChair)!=NULL);
			IServerUserItem * pIServerUserItem=GetTableUserItem(wOffLineChair);

			//结束游戏
			if (pIServerUserItem!=NULL)
			{
				//设置变量
				m_dwOffLineTime[wOffLineChair]=0L;

				//用户起立
				LOGI("CTableFrame::OnEventTimer PerformStandUpAction IDI_OFF_LINE, NickName="<<pIServerUserItem->GetNickName());
				PerformStandUpAction(pIServerUserItem);
			}

			//继续时间
			if (m_bGameStarted==true)
			{
				//变量定义
				DWORD dwOffLineTime=0L;
				WORD wOffLineChair=INVALID_CHAIR;

				//寻找用户
				for (WORD i=0;i<m_wChairCount;i++)
				{
					if ((m_dwOffLineTime[i]!=0L)&&((m_dwOffLineTime[i]<dwOffLineTime)||(wOffLineChair==INVALID_CHAIR)))
					{
						wOffLineChair=i;
						dwOffLineTime=m_dwOffLineTime[i];
					}
				}

				//设置时间
				if (wOffLineChair!=INVALID_CHAIR)
				{
					//时间计算
					DWORD dwCurrentTime=(DWORD)time(NULL);
					DWORD dwLapseTime=dwCurrentTime-m_dwOffLineTime[wOffLineChair];

					//设置时间
					dwLapseTime=__min(dwLapseTime,TIME_OFF_LINE-2000L);
					LOGI("CTableFrame::OnEventTimer wOffLineChair!=INVALID_CHAIR SetGameTimer time="<<TIME_OFF_LINE-dwLapseTime);
					SetGameTimer(IDI_OFF_LINE,TIME_OFF_LINE-dwLapseTime,1,wOffLineChair);
				}
			}

			return true;
		}
	case IDI_GAME_TIME:
		{
			DismissGame();
			KillGameTimer(IDI_GAME_TIME);
			return true;
		}
	case IDI_CUSTOMTABLE_OVERDUE:
		{
			CheckCustomTableOverdue();

			return true;
		}
	case IDI_CHECK_DISMISS_TABLE:
		{
			for (WORD i = 0; i < GetChairCount(); i++)
			{
				IServerUserItem* pUserItem = GetTableUserItem(i);
				if (pUserItem == nullptr)
				{
					continue;
				}

				auto iter = m_mapDismissVote.find(i);
				if (iter != m_mapDismissVote.end())
				{
					continue;
				}

				// 未投票则默认投赞同票
				m_mapDismissVote.insert(make_pair(i, true));
			}

			// 检查投票
			CheckVoteForDismiss();
			return true;
		}
	}

	//错误断言
	ASSERT(FALSE);

	return false;
}

//游戏事件
bool CTableFrame::OnEventSocketGame(WORD wSubCmdID, VOID * pData, WORD wDataSize, IServerUserItem * pIServerUserItem)
{
	//效验参数
	ASSERT(pIServerUserItem!=NULL);
	ASSERT(m_pITableFrameSink!=NULL);

	//消息处理
	return m_pITableFrameSink->OnGameMessage(wSubCmdID,pData,wDataSize,pIServerUserItem);
}

//框架事件
bool CTableFrame::OnEventSocketFrame(WORD wSubCmdID, VOID * pData, WORD wDataSize, IServerUserItem * pIServerUserItem)
{
	//游戏处理
	if (m_pITableFrameSink->OnFrameMessage(wSubCmdID,pData,wDataSize,pIServerUserItem)==true) return true;

	//比赛处理
	if(m_pIGameMatchSink!=NULL && m_pIGameMatchSink->OnFrameMessage(wSubCmdID,pData,wDataSize,pIServerUserItem)==true) return true;

	//默认处理
	switch (wSubCmdID)
	{
	case SUB_GF_GAME_OPTION:	//游戏配置
		{
			//效验参数
			ASSERT(wDataSize==sizeof(CMD_GF_GameOption));
			if (wDataSize!=sizeof(CMD_GF_GameOption)) return false;

			//变量定义
			CMD_GF_GameOption * pGameOption=(CMD_GF_GameOption *)pData;

			//获取属性
			WORD wChairID=pIServerUserItem->GetChairID();
			BYTE cbUserStatus=pIServerUserItem->GetUserStatus();

			//断线清理
			if ((cbUserStatus!=US_LOOKON)&&((m_dwOffLineTime[wChairID]!=0L)))
			{
				//设置变量
				m_dwOffLineTime[wChairID]=0L;

				//删除时间
				WORD wOffLineCount=GetOffLineUserCount();
				if (wOffLineCount==0)
				{
					LOGI("CTableFrame::OnEventSocketFrame, KillGameTimer(IDI_OFF_LINE)");
					KillGameTimer(IDI_OFF_LINE);
				}
			}

			//设置状态
			pIServerUserItem->SetClientReady(true);
			if (cbUserStatus!=US_LOOKON) m_bAllowLookon[wChairID]=pGameOption->cbAllowLookon?true:false;

			//桌子创建者
			if (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM)
			{
				CMD_GF_CustomTableInfo CustomTableInfo;
				CustomTableInfo.dwUserID = m_pGameTableOption->dwCreatUserID;
				CustomTableInfo.bTableStarted = m_bTableStarted;
				CustomTableInfo.dwRoundCount = m_pGameTableOption->dwRoundCount;
				CustomTableInfo.dwPlayRoundCount = m_wDrawCount;
				lstrcpyn(CustomTableInfo.szSecret, m_pGameTableOption->szSecret, sizeof(CustomTableInfo));
				m_pIMainServiceFrame->SendData(pIServerUserItem, MDM_GF_FRAME, SUB_GF_CUSTOMTABLE_INFO, &CustomTableInfo, sizeof(CustomTableInfo));

				// 检查是否有同IP的玩家在桌子上
				if (cbUserStatus != US_LOOKON)
				{
					EfficacyIPAddress();
				}
			}

			//发送状态
			CMD_GF_GameStatus GameStatus;
			GameStatus.cbGameStatus=m_cbGameStatus;
			GameStatus.cbAllowLookon=m_bAllowLookon[wChairID]?TRUE:FALSE;
			m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GF_FRAME,SUB_GF_GAME_STATUS,&GameStatus,sizeof(GameStatus));

			//发送消息
			TCHAR szMessage[128]=TEXT("");
			_sntprintf(szMessage,CountArray(szMessage),TEXT("欢迎您进入“%s-%s”游戏，祝您游戏愉快！"),m_pGameServiceAttrib->szGameName,m_pGameServiceOption->szServerName);
			m_pIMainServiceFrame->SendGameMessage(pIServerUserItem,szMessage,SMT_CHAT);

			//发送场景
			bool bSendSecret=((cbUserStatus!=US_LOOKON)||(m_bAllowLookon[wChairID]==true));
			m_pITableFrameSink->OnEventSendGameScene(wChairID,pIServerUserItem,m_cbGameStatus,bSendSecret);

			//开始判断
			if ((cbUserStatus==US_READY)&&(EfficacyStartGame(wChairID)==true))
			{
				StartGame(); 
			}

			return true;
		}
	case SUB_GF_USER_READY:		//用户准备
		{
			//获取属性
			WORD wChairID=pIServerUserItem->GetChairID();
			BYTE cbUserStatus=pIServerUserItem->GetUserStatus();

			//效验状态
			ASSERT(GetTableUserItem(wChairID)==pIServerUserItem);
			if (GetTableUserItem(wChairID)!=pIServerUserItem) return false;

			//效验状态
			//ASSERT(cbUserStatus==US_SIT);
			if (cbUserStatus!=US_SIT) return true;

			//防作弊分组判断
			if((CServerRule::IsAllowAvertCheatMode(m_pGameServiceOption->dwServerRule)&&(m_pGameServiceAttrib->wChairCount < MAX_CHAIR))
				&& (m_wDrawCount >= m_pGameServiceOption->wDistributeDrawCount || CheckDistribute()))
			{
				//发送消息
				LPCTSTR pszMessage=TEXT("系统重新分配桌子，请稍后！");
				SendGameMessage(pIServerUserItem,pszMessage,SMT_CHAT);

				//发送消息
				m_pIMainServiceFrame->InsertWaitDistribute(pIServerUserItem);

				//用户起立
				LOGI("CTableFrame::OnEventTimer PerformStandUpAction SUB_GF_USER_READY, NickName="<<pIServerUserItem->GetNickName());
				PerformStandUpAction(pIServerUserItem);

				return true;
			}

			//事件通知
			if (m_pITableUserAction!=NULL)
			{
				m_pITableUserAction->OnActionUserOnReady(wChairID,pIServerUserItem,pData,wDataSize);
			}

			//事件通知
			if(m_pIMatchTableAction!=NULL && !m_pIMatchTableAction->OnActionUserOnReady(wChairID,pIServerUserItem, pData,wDataSize))
				return true;

			//开始判断
			if (EfficacyStartGame(wChairID)==false)
			{
				pIServerUserItem->SetUserStatus(US_READY,m_wTableID,wChairID);
			}
			else
			{
				StartGame(); 
			}

			return true;
		}
	case SUB_GF_USER_CHAT:		//用户聊天
		{
			//变量定义
			CMD_GF_C_UserChat * pUserChat=(CMD_GF_C_UserChat *)pData;

			//效验参数
			ASSERT(wDataSize<=sizeof(CMD_GF_C_UserChat));
			ASSERT(wDataSize>=(sizeof(CMD_GF_C_UserChat)-sizeof(pUserChat->szChatString)));
			ASSERT(wDataSize==(sizeof(CMD_GF_C_UserChat)-sizeof(pUserChat->szChatString)+pUserChat->wChatLength*sizeof(pUserChat->szChatString[0])));

			//效验参数
			if (wDataSize>sizeof(CMD_GF_C_UserChat)) return false;
			if (wDataSize<(sizeof(CMD_GF_C_UserChat)-sizeof(pUserChat->szChatString))) return false;
			if (wDataSize!=(sizeof(CMD_GF_C_UserChat)-sizeof(pUserChat->szChatString)+pUserChat->wChatLength*sizeof(pUserChat->szChatString[0]))) return false;

			//目标用户
			if ((pUserChat->dwTargetUserID!=0)&&(SearchUserItem(pUserChat->dwTargetUserID)==NULL))
			{
				ASSERT(FALSE);
				return true;
			}

			//状态判断
			if ((CServerRule::IsForfendGameChat(m_pGameServiceOption->dwServerRule)==true)&&(pIServerUserItem->GetMasterOrder()==0L))
			{
				SendGameMessage(pIServerUserItem,TEXT("抱歉，当前游戏房间禁止游戏聊天！"),SMT_CHAT);
				return true;
			}

			//权限判断
			if (CUserRight::CanRoomChat(pIServerUserItem->GetUserRight())==false)
			{
				SendGameMessage(pIServerUserItem,TEXT("抱歉，您没有游戏聊天的权限，若需要帮助，请联系游戏客服咨询！"),SMT_EJECT|SMT_CHAT);
				return true;
			}

			//构造消息
			CMD_GF_S_UserChat UserChat;
			ZeroMemory(&UserChat,sizeof(UserChat));

			//字符过滤
			m_pIMainServiceFrame->SensitiveWordFilter(pUserChat->szChatString,UserChat.szChatString,CountArray(UserChat.szChatString));

			//构造数据
			UserChat.dwChatColor=pUserChat->dwChatColor;
			UserChat.wChatLength=pUserChat->wChatLength;
			UserChat.dwTargetUserID=pUserChat->dwTargetUserID;
			UserChat.dwSendUserID=pIServerUserItem->GetUserID();
			UserChat.wChatLength=CountStringBuffer(UserChat.szChatString);

			//发送数据
			WORD wHeadSize=sizeof(UserChat)-sizeof(UserChat.szChatString);
			WORD wSendSize=wHeadSize+UserChat.wChatLength*sizeof(UserChat.szChatString[0]);

			//游戏用户
			for (WORD i=0;i<m_wChairCount;i++)
			{
				//获取用户
				IServerUserItem * pIServerUserItem=GetTableUserItem(i);
				if ((pIServerUserItem==NULL)||(pIServerUserItem->IsClientReady()==false)) continue;

				m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GF_FRAME,SUB_GF_USER_CHAT,&UserChat,wSendSize);
			}

			//旁观用户
			WORD wEnumIndex=0;
			IServerUserItem * pIServerUserItem=NULL;

			//枚举用户
			do
			{
				//获取用户
				pIServerUserItem=EnumLookonUserItem(wEnumIndex++);
				if (pIServerUserItem==NULL) break;

				//发送数据
				if (pIServerUserItem->IsClientReady()==true)
				{
					m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GF_FRAME,SUB_GF_USER_CHAT,&UserChat,wSendSize);
				}
			} while (true);

			return true;
		}
	case SUB_GF_USER_EXPRESSION:	//用户表情
		{
			//效验参数
			ASSERT(wDataSize==sizeof(CMD_GF_C_UserExpression));
			if (wDataSize!=sizeof(CMD_GF_C_UserExpression)) return false;

			//变量定义
			CMD_GF_C_UserExpression * pUserExpression=(CMD_GF_C_UserExpression *)pData;

			//目标用户
			if ((pUserExpression->dwTargetUserID!=0)&&(SearchUserItem(pUserExpression->dwTargetUserID)==NULL))
			{
				ASSERT(FALSE);
				return true;
			}

			//状态判断
			if ((CServerRule::IsForfendGameChat(m_pGameServiceOption->dwServerRule)==true)&&(pIServerUserItem->GetMasterOrder()==0L))
			{
				SendGameMessage(pIServerUserItem,TEXT("抱歉，当前游戏房间禁止游戏聊天！"),SMT_CHAT);
				return true;
			}

			//权限判断
			if (CUserRight::CanRoomChat(pIServerUserItem->GetUserRight())==false)
			{
				SendGameMessage(pIServerUserItem,TEXT("抱歉，您没有游戏聊天的权限，若需要帮助，请联系游戏客服咨询！"),SMT_EJECT|SMT_CHAT);
				return true;
			}

			//构造消息
			CMD_GR_S_UserExpression UserExpression;
			ZeroMemory(&UserExpression,sizeof(UserExpression));

			//构造数据
			UserExpression.wItemIndex=pUserExpression->wItemIndex;
			UserExpression.dwSendUserID=pIServerUserItem->GetUserID();
			UserExpression.dwTargetUserID=pUserExpression->dwTargetUserID;

			//游戏用户
			for (WORD i=0;i<m_wChairCount;i++)
			{
				//获取用户
				IServerUserItem * pIServerUserItem=GetTableUserItem(i);
				if ((pIServerUserItem==NULL)||(pIServerUserItem->IsClientReady()==false)) continue;

				//发送数据
				m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GF_FRAME,SUB_GF_USER_EXPRESSION,&UserExpression,sizeof(UserExpression));
			}

			//旁观用户
			WORD wEnumIndex=0;
			IServerUserItem * pIServerUserItem=NULL;

			//枚举用户
			do
			{
				//获取用户
				pIServerUserItem=EnumLookonUserItem(wEnumIndex++);
				if (pIServerUserItem==NULL) break;

				//发送数据
				if (pIServerUserItem->IsClientReady()==true)
				{
					m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GF_FRAME,SUB_GF_USER_EXPRESSION,&UserExpression,sizeof(UserExpression));
				}
			} while (true);

			return true;
		}
	case SUB_GF_LOOKON_CONFIG:		//旁观配置
		{
			//效验参数
			ASSERT(wDataSize==sizeof(CMD_GF_LookonConfig));
			if (wDataSize<sizeof(CMD_GF_LookonConfig)) return false;

			//变量定义
			CMD_GF_LookonConfig * pLookonConfig=(CMD_GF_LookonConfig *)pData;

			//目标用户
			if ((pLookonConfig->dwUserID!=0)&&(SearchUserItem(pLookonConfig->dwUserID)==NULL))
			{
				ASSERT(FALSE);
				return true;
			}

			//用户效验
			ASSERT(pIServerUserItem->GetUserStatus()!=US_LOOKON);
			if (pIServerUserItem->GetUserStatus()==US_LOOKON) return false;

			//旁观处理
			if (pLookonConfig->dwUserID!=0L)
			{

				for (INT_PTR i=0;i<m_LookonUserItemArray.GetCount();i++)
				{
					//获取用户
					IServerUserItem * pILookonUserItem=m_LookonUserItemArray[i];
					if (pILookonUserItem->GetUserID()!=pLookonConfig->dwUserID) continue;
					if (pILookonUserItem->GetChairID()!=pIServerUserItem->GetChairID()) continue;

					//构造消息
					CMD_GF_LookonStatus LookonStatus;
					LookonStatus.cbAllowLookon=pLookonConfig->cbAllowLookon;

					//发送消息
					ASSERT(m_pIMainServiceFrame!=NULL);
					m_pIMainServiceFrame->SendData(pILookonUserItem,MDM_GF_FRAME,SUB_GF_LOOKON_STATUS,&LookonStatus,sizeof(LookonStatus));

					break;
				}

				//事件通知
				bool bAllowLookon=(pLookonConfig->cbAllowLookon==TRUE)?true:false;
				if (m_pITableUserAction!=NULL)
				{
					m_pITableUserAction->OnActionUserAllowLookon(pIServerUserItem->GetChairID(), pIServerUserItem, bAllowLookon);
				}
			}
			else
			{
				//设置判断
				bool bAllowLookon=(pLookonConfig->cbAllowLookon==TRUE)?true:false;
				if (bAllowLookon==m_bAllowLookon[pIServerUserItem->GetChairID()]) return true;

				//设置变量
				m_bAllowLookon[pIServerUserItem->GetChairID()]=bAllowLookon;

				//构造消息
				CMD_GF_LookonStatus LookonStatus;
				LookonStatus.cbAllowLookon=pLookonConfig->cbAllowLookon;

				//发送消息
				for (INT_PTR i=0;i<m_LookonUserItemArray.GetCount();i++)
				{
					//获取用户
					IServerUserItem * pILookonUserItem=m_LookonUserItemArray[i];
					if (pILookonUserItem->GetChairID()!=pIServerUserItem->GetChairID()) continue;

					//发送消息
					ASSERT(m_pIMainServiceFrame!=NULL);
					m_pIMainServiceFrame->SendData(pILookonUserItem,MDM_GF_FRAME,SUB_GF_LOOKON_STATUS,&LookonStatus,sizeof(LookonStatus));
				}

				//事件通知
				if (m_pITableUserAction!=NULL)
				{
					m_pITableUserAction->OnActionUserAllowLookon(pIServerUserItem->GetChairID(), pIServerUserItem, bAllowLookon);
				}
			}

			return true;
		}
	case SUB_GF_APPLY_FOR_DISMISS:
		{
			// 玩家申请解散
			return OnEventSocketFrameApplyForDismiss(pData, wDataSize, pIServerUserItem);
		}
		break;
	case SUB_GF_CHOICE_FOR_DISMISS:
		{
			// 玩家的解散选择
			return OnEventSocketFrameChoiceForDismiss(pData, wDataSize, pIServerUserItem);
		}
		break;
	}

	return false;
}

// 玩家申请解散
bool CTableFrame::OnEventSocketFrameApplyForDismiss(VOID * pData, WORD wDataSize, IServerUserItem * pIServerUserItem)
{
	// 申请解散房间
	// 效验参数
	ASSERT(wDataSize == sizeof(CMD_GF_C_UserApplyForDismiss));
	if (wDataSize != sizeof(CMD_GF_C_UserApplyForDismiss)) return false;

	if (m_bLockedForDismiss == true)
	{
		return true;
	}

	CMD_GF_C_UserApplyForDismiss* pUserApplyForDismiss = (CMD_GF_C_UserApplyForDismiss*)pData;

	// 如果开始过游戏,解散就需要同桌玩家同意
	if (m_bTableStarted == false)
	{
		// 如果没有开始过游戏,并且是桌主自己提出解散申请,直接解散
		if (pUserApplyForDismiss->dwUserID == m_pGameTableOption->dwCreatUserID)
		{
			DismissCustomTable(CONCLUDE_TYPE_CREATER_DISMISS);
		}
	}
	else
	{
		// 如果玩家已经投过票了,提示不要重复处理
		IServerUserItem* pIServerUserItem = SearchUserItem(pUserApplyForDismiss->dwUserID);
		if (pIServerUserItem == nullptr)
		{
			return true;
		}
		//游戏用户
		IServerUserItem* pITableUserItem = GetTableUserItem(pIServerUserItem->GetChairID());
		if (pITableUserItem != pIServerUserItem)
		{
			return true;
		}

		// 统计投票结果
		auto iter = m_mapDismissVote.find(pITableUserItem->GetChairID());
		if (iter != m_mapDismissVote.end())
		{
			SendGameMessage(pITableUserItem, TEXT("投票处理中,请等待其它玩家投票."), SMT_CHAT | SMT_EJECT);
			return true;
		}
		bool bfirstvote = false;
		if (m_mapDismissVote.empty())
		{
			// 之前没有投票结果,是第一次发起申请
			bfirstvote = true;

			// 开启定时器
			SetGameTimer(IDI_CHECK_DISMISS_TABLE, CHECK_DISMISS_TABLE_TIME, 1, NULL);
		}
		m_mapDismissVote.insert(make_pair(pITableUserItem->GetChairID(), true));

		// 转发消息给房间所有非旁观玩家
		if (bfirstvote )
		{
			CMD_GF_S_UserApplyForDismiss UserApplyForDismiss;
			UserApplyForDismiss.dwUserID = pUserApplyForDismiss->dwUserID;
			for (WORD i = 0; i < GetChairCount(); i++)
			{
				IServerUserItem* pServerUserItem = GetTableUserItem(i);
				if (pServerUserItem == nullptr)
				{
					continue;
				}
				if (pServerUserItem->GetUserID() == pUserApplyForDismiss->dwUserID)
				{
					// 通知申请者,解散请求已发送
					SendGameMessage(pServerUserItem, TEXT("解散请求已发送给同桌的其他玩家,请等待投票结果."), SMT_CHAT | SMT_EJECT);
				}
				else
				{
					SendTableData(i, SUB_GF_APPLY_FOR_DISMISS, &UserApplyForDismiss, sizeof(UserApplyForDismiss), MDM_GF_FRAME);
				}
			}
		}
		else
		{
			// 转发消息给房间所有非旁观玩家
			CMD_GF_S_UserChoiceForDismiss UserChoiceForDismiss;
			UserChoiceForDismiss.dwUserID = pITableUserItem->GetUserID();
			UserChoiceForDismiss.bDismiss = true;
			for (WORD i = 0; i < GetChairCount(); i++)
			{
				IServerUserItem* pServerUserItem = GetTableUserItem(i);
				if (pServerUserItem == nullptr || pServerUserItem->GetUserID() == pITableUserItem->GetUserID())
				{
					continue;
				}
				SendTableData(i, SUB_GF_CHOICE_FOR_DISMISS, &UserChoiceForDismiss, sizeof(UserChoiceForDismiss), MDM_GF_FRAME);
			}

			CheckVoteForDismiss();
		}
	}

	return true;
}

// 玩家解散事件的投票结果
bool CTableFrame::OnEventSocketFrameChoiceForDismiss(VOID * pData, WORD wDataSize, IServerUserItem * pIServerUserItem)
{
	//效验参数
	ASSERT(wDataSize == sizeof(CMD_GF_C_UserChoiceForDismiss));
	if (wDataSize != sizeof(CMD_GF_C_UserChoiceForDismiss)) return false;

	if (m_mapDismissVote.empty())
	{
		// 如果投票结果为空,说明没有人申请解散.因为申请解散后,默认会插入申请者的选项
		return true;
	}

	CMD_GF_C_UserChoiceForDismiss* pUserChoiceForDismiss = (CMD_GF_C_UserChoiceForDismiss*)pData;

	WORD wVoteSite = 0xFF;
	for (WORD i = 0; i < GetChairCount(); i++)
	{
		IServerUserItem* pUserItem = GetTableUserItem(i);
		if (GetTableUserItem(i) == pIServerUserItem)
		{
			wVoteSite = i;
			break;
		}
	}
	auto iter = m_mapDismissVote.find(wVoteSite);
	if (iter != m_mapDismissVote.end())
	{
		SendGameMessage(pIServerUserItem, TEXT("您已经投过票了,不能再次投票."), SMT_CHAT | SMT_EJECT);
		return true;
	}

	if (pUserChoiceForDismiss->bDismiss == false)
	{
		// 关闭定时器
		KillGameTimer(IDI_CHECK_DISMISS_TABLE);
		// 清理投票结果
		m_bLockedForDismiss = false;
		m_wConcludeType = INVALID_WORD;
		m_mapDismissVote.clear();
		// 通知玩家
		TCHAR szMessage[128] = { 0 };
		_snprintf(szMessage, sizeof(szMessage), TEXT("玩家[%s]不同意解散游戏,游戏继续."), pIServerUserItem->GetNickName());
		SendGameMessage(szMessage, SMT_CHAT | SMT_EJECT);
	}
	else
	{
		// 统计投票结果
		for (WORD i = 0; i < GetChairCount(); i++)
		{
			IServerUserItem* pUserItem = GetTableUserItem(i);
			if (pUserItem == nullptr || pUserItem->GetUserID() != pUserChoiceForDismiss->dwUserID)
			{
				continue;
			}

			m_mapDismissVote.insert(make_pair(i, pUserChoiceForDismiss->bDismiss));
		}

		// 转发消息给房间所有非旁观玩家
		CMD_GF_S_UserChoiceForDismiss UserChoiceForDismiss;
		UserChoiceForDismiss.dwUserID = pUserChoiceForDismiss->dwUserID;
		UserChoiceForDismiss.bDismiss = pUserChoiceForDismiss->bDismiss;
		for (WORD i = 0; i < GetChairCount(); i++)
		{
			IServerUserItem* pServerUserItem = GetTableUserItem(i);
			if (pServerUserItem == nullptr || pServerUserItem->GetUserID() == pUserChoiceForDismiss->dwUserID)
			{
				continue;
			}
			SendTableData(i, SUB_GF_CHOICE_FOR_DISMISS, &UserChoiceForDismiss, sizeof(UserChoiceForDismiss), MDM_GF_FRAME);
		}

		CheckVoteForDismiss();
	}

	return true;
}

// 检查解散桌子相关投票
void CTableFrame::CheckVoteForDismiss()
{
	// 检查投票结果
	WORD wTotalCount = 0;
	WORD wAgreeCount = 0;
	WORD wDisagreeCount = 0;
	for (WORD i = 0; i < GetChairCount(); i++)
	{
		IServerUserItem* pUserItem = GetTableUserItem(i);
		if (pUserItem == nullptr || pUserItem->GetUserStatus() < US_PLAYING)
		{
			continue;
		}
		wTotalCount++;
		auto iter = m_mapDismissVote.find(i);
		if (iter != m_mapDismissVote.end())
		{
			if (iter->second == true)
			{
				wAgreeCount++;
			}
			else
			{
				wDisagreeCount++;
			}
		}
	}

	if (wAgreeCount + wDisagreeCount >= wTotalCount - 1)
	{
		// 关闭定时器
		KillGameTimer(IDI_CHECK_DISMISS_TABLE);
		// 清理投票结果
		m_wConcludeType = INVALID_WORD;
		m_mapDismissVote.clear();
		// 解散桌子
		DismissCustomTable(CONCLUDE_TYPE_PLAYER_DISMISS);
	}
}

// 解散自建桌子
void CTableFrame::DismissCustomTable(WORD wConcludeType)
{
	// 锁定桌子,不能开始游戏
	m_bLockedForDismiss = true;
	m_wConcludeType = wConcludeType;

	// 通知游戏服务器解散房间
	DBR_GR_ConcludeCustomTable DismissCustomTable;
	DismissCustomTable.wTableID = m_wTableID;
	DismissCustomTable.wServerID = m_pGameServiceOption->wServerID;
	DismissCustomTable.wConcludeType = wConcludeType;
	if (m_pIKernelDataBaseEngine)
	{
		m_pIKernelDataBaseEngine->PostDataBaseRequest(DBR_GR_CONCLUDE_CUSTOMTABLE, NULL, &DismissCustomTable, sizeof(DismissCustomTable));
	}
}

// 检查是否超时,解散桌子
bool CTableFrame::CheckCustomTableOverdue()
{
	if (m_bTableStarted == true)
	{
		// 已经开始过游戏,24小时未结束的话就强制结束
		// 检查自建桌子是否过期
		CTime tmLocalTime = CTime::GetCurrentTime();

		if (CTime(m_pGameTableOption->TableCreateTime) + CTimeSpan(0, 24, 45, 0) <= tmLocalTime)
		{
			TCHAR szMessage[2048] = { 0 };
			_snprintf(szMessage, sizeof(szMessage), TEXT("CTableFrame::CheckCustomTableOverdue, Table Is Start. TableCreateTime(%d-%d-%d %d:%d:%d), LocalTime(%d-%d-%d %d:%d:%d), \
							CreateUser=%d, Secret=%s, RoundCount=%d"), \
							m_pGameTableOption->TableCreateTime.wYear, m_pGameTableOption->TableCreateTime.wMonth, m_pGameTableOption->TableCreateTime.wDay, \
							m_pGameTableOption->TableCreateTime.wHour, m_pGameTableOption->TableCreateTime.wMinute, m_pGameTableOption->TableCreateTime.wMilliseconds, \
							tmLocalTime.GetYear(), tmLocalTime.GetMonth(), tmLocalTime.GetDay(), tmLocalTime.GetHour(), tmLocalTime.GetMinute(), tmLocalTime.GetSecond(), \
							m_pGameTableOption->dwCreatUserID, m_pGameTableOption->szSecret, m_pGameTableOption->dwRoundCount);

			LOGI(szMessage);
			// 准备解散
			DismissCustomTable(CONCLUDE_TYPE_TIMEOUT_START);

			KillGameTimer(IDI_CUSTOMTABLE_OVERDUE);

			return true;
		}
		else if (CTime(m_pGameTableOption->TableCreateTime) + CTimeSpan(0, 23, 44, 0) <= tmLocalTime)
		{
			// 提示将在1分钟后解散
			SendGameMessage(TEXT("自建房间长时间未结束,将在一分钟后结束!"), SMT_CHAT | SMT_TABLE_ROLL);
			return true;
		}
		else if (CTime(m_pGameTableOption->TableCreateTime) + CTimeSpan(0, 23, 40, 0) <= tmLocalTime)
		{
			// 提示将在5分钟后解散
			SendGameMessage(TEXT("自建房间长时间未结束,将在五分钟后结束!"), SMT_CHAT | SMT_TABLE_ROLL);
			return true;
		}
	}
	else
	{
// 	}
// 	if (m_bGameStarted == false && m_bDrawStarted == false && m_bTableStarted == false)
//	{
		// 检查自建桌子是否过期
		CTime tmLocalTime = CTime::GetCurrentTime();

		if (CTime(m_pGameTableOption->TableCreateTime) + CTimeSpan(0, 0, 15, 0) <= tmLocalTime)
		{
			TCHAR szMessage[2048] = { 0 };
			_snprintf(szMessage, sizeof(szMessage), TEXT("CTableFrame::CheckCustomTableOverdue, Table Is Not Start TableCreateTime(%d-%d-%d %d:%d:%d), LocalTime(%d-%d-%d %d:%d:%d), \
					CreateUser=%d, Secret=%s, RoundCount=%d"), \
				m_pGameTableOption->TableCreateTime.wYear, m_pGameTableOption->TableCreateTime.wMonth, m_pGameTableOption->TableCreateTime.wDay,\
				m_pGameTableOption->TableCreateTime.wHour, m_pGameTableOption->TableCreateTime.wMinute, m_pGameTableOption->TableCreateTime.wMilliseconds, \
				tmLocalTime.GetYear(), tmLocalTime.GetMonth(), tmLocalTime.GetDay(), tmLocalTime.GetHour(), tmLocalTime.GetMinute(), tmLocalTime.GetSecond(),  \
				m_pGameTableOption->dwCreatUserID, m_pGameTableOption->szSecret, m_pGameTableOption->dwRoundCount);

			LOGI(szMessage);
			// 准备解散
			DismissCustomTable(CONCLUDE_TYPE_TIMEOUT_NOSTART);

			KillGameTimer(IDI_CUSTOMTABLE_OVERDUE);

			return true;
		}
		else if (CTime(m_pGameTableOption->TableCreateTime) + CTimeSpan(0, 0, 14, 0) <= tmLocalTime)
		{
			// 提示将在1分钟后解散
			SendGameMessage(TEXT("游戏长时间未开始,将在一分钟后解散!若游戏未开始,解散房间不会扣除钻石"), SMT_CHAT | SMT_TABLE_ROLL);
			return true;
		}
		else if (CTime(m_pGameTableOption->TableCreateTime) + CTimeSpan(0, 0, 10, 0) <= tmLocalTime)
		{
			// 提示将在5分钟后解散
			SendGameMessage(TEXT("游戏长时间未开始,将在五分钟后解散!若游戏未开始,解散房间不会扣除钻石"), SMT_CHAT | SMT_TABLE_ROLL);
			return true;
		}
	}

	return true;
}

//获取空位
WORD CTableFrame::GetNullChairID()
{
	//椅子搜索
	for (WORD i=0;i<m_wChairCount;i++)
	{
		if (m_TableUserItemArray[i]==NULL)
		{
			return i;
		}
	}

	return INVALID_CHAIR;
}

//随机空位
WORD CTableFrame::GetRandNullChairID()
{
	//椅子搜索
	WORD wIndex = rand()%m_wChairCount;
	for (WORD i=wIndex;i<m_wChairCount+wIndex;i++)
	{
		if (m_TableUserItemArray[i%m_wChairCount]==NULL)
		{
			return i%m_wChairCount;
		}
	}

	return INVALID_CHAIR;
}

//用户数目
WORD CTableFrame::GetSitUserCount()
{
	//变量定义
	WORD wUserCount=0;

	//数目统计
	for (WORD i=0;i<m_wChairCount;i++)
	{
		if (GetTableUserItem(i)!=NULL)
		{
			wUserCount++;
		}
	}

	return wUserCount;
}

//旁观数目
WORD CTableFrame::GetLookonUserCount()
{
	//获取数目
	INT_PTR nLookonCount=m_LookonUserItemArray.GetCount();

	return (WORD)(nLookonCount);
}

//断线数目
WORD CTableFrame::GetOffLineUserCount()
{
	//变量定义
	WORD wOffLineCount=0;

	//断线人数
	for (WORD i=0;i<m_wChairCount;i++)
	{
		if (m_dwOffLineTime[i]!=0L)
		{
			wOffLineCount++;
		}
	}

	return wOffLineCount;
}

//桌子状况
WORD CTableFrame::GetTableUserInfo(tagTableUserInfo & TableUserInfo)
{
	//设置变量
	ZeroMemory(&TableUserInfo,sizeof(TableUserInfo));

	//用户分析
	for (WORD i=0;i<m_pGameServiceAttrib->wChairCount;i++)
	{
		//获取用户
		IServerUserItem * pIServerUserItem=GetTableUserItem(i);
		if (pIServerUserItem==NULL) continue;

		//用户类型
		if (pIServerUserItem->IsAndroidUser()==false)
		{
			TableUserInfo.wTableUserCount++;
		}
		else
		{
			TableUserInfo.wTableAndroidCount++;
		}

		//准备判断
		if (pIServerUserItem->GetUserStatus()==US_READY)
		{
			TableUserInfo.wTableReadyCount++;
		}
	}

	//最少数目
	switch (m_cbStartMode)
	{
	case START_MODE_ALL_READY:		//所有准备
		{
			TableUserInfo.wMinUserCount=2;
			break;
		}
	case START_MODE_PAIR_READY:		//配对开始
		{
			TableUserInfo.wMinUserCount=2;
			break;
		}
	case START_MODE_TIME_CONTROL:	//时间控制
		{
			TableUserInfo.wMinUserCount=1;
			break;
		}
	default:						//默认模式
		{
			TableUserInfo.wMinUserCount=m_pGameServiceAttrib->wChairCount;
			break;
		}
	}

	return TableUserInfo.wTableAndroidCount+TableUserInfo.wTableUserCount;
}

//配置桌子
bool CTableFrame::InitializationFrame(WORD wTableID, tagTableFrameParameter & TableFrameParameter)
{
	//设置变量
	m_wTableID=wTableID;
	m_wChairCount=TableFrameParameter.pGameServiceAttrib->wChairCount;

	//配置参数
	m_pGameParameter=TableFrameParameter.pGameParameter;
	m_pGameServiceAttrib=TableFrameParameter.pGameServiceAttrib;
	m_pGameServiceOption=TableFrameParameter.pGameServiceOption;
//	m_pGameServiceTableOption = TableFrameParameter.pGameServiceTableOption;
//	m_GameTableOption = TableFrameParameter.pGameServiceTableOption->TableOption[wTableID];
	m_pGameTableOption = TableFrameParameter.pTableOption;

	//组件接口
	m_pITimerEngine=TableFrameParameter.pITimerEngine;
	m_pIMainServiceFrame=TableFrameParameter.pIMainServiceFrame;
	m_pIAndroidUserManager=TableFrameParameter.pIAndroidUserManager;
	m_pIKernelDataBaseEngine=TableFrameParameter.pIKernelDataBaseEngine;
	m_pIRecordDataBaseEngine=TableFrameParameter.pIRecordDataBaseEngine;

	//创建桌子
	IGameServiceManager * pIGameServiceManager=TableFrameParameter.pIGameServiceManager;
	if (m_pITableFrameSink == NULL)
	{
		m_pITableFrameSink = (ITableFrameSink *)pIGameServiceManager->CreateTableFrameSink(IID_ITableFrameSink, VER_ITableFrameSink);

		//错误判断
		if (m_pITableFrameSink == NULL)
		{
			ASSERT(FALSE);
			return false;
		}

		//设置桌子
		IUnknownEx * pITableFrame = QUERY_ME_INTERFACE(IUnknownEx);
		if (m_pITableFrameSink->Initialization(pITableFrame) == false) return false;
	}
	else
	{
		// 重置数据
		m_pITableFrameSink->RepositionSink();

		//设置桌子
		IUnknownEx * pITableFrame = QUERY_ME_INTERFACE(IUnknownEx);
		if (m_pITableFrameSink->ReInitialization(pITableFrame) == false) return false;
	}

	//设置变量
	m_lCellScore = m_pGameTableOption->lCellScore;
	if (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM)
	{
		if (m_pGameTableOption->dwPlayRoundCount > 0)
		{
			// 开始过游戏,设置桌子状态
			m_wDrawCount = m_pGameTableOption->dwPlayRoundCount - 1;
			m_bTableStarted = true;
		}
	}

	if (m_pGameTableOption && m_pGameTableOption->szSecret[0] != 0)
	{
		m_bLockedForCustom = true;

		// 启动定时器
		SetGameTimer(IDI_CUSTOMTABLE_OVERDUE, CUSTOMTABLE_OVERDUE_FREQUENCY, TIMES_INFINITY, NULL);

		// 检查桌子是否超时
		CheckCustomTableOverdue();
	}

	//扩展接口
	m_pITableUserAction=QUERY_OBJECT_PTR_INTERFACE(m_pITableFrameSink,ITableUserAction);
	m_pITableUserRequest=QUERY_OBJECT_PTR_INTERFACE(m_pITableFrameSink,ITableUserRequest);

	//创建比赛模式
	if((TableFrameParameter.pGameServiceOption->wServerType&GAME_GENRE_MATCH)!=0 && TableFrameParameter.pIGameMatchServiceManager!=NULL)
	{
		IUnknownEx * pIUnknownEx=QUERY_ME_INTERFACE(IUnknownEx);
		IGameMatchServiceManager * pIGameMatchServiceManager=QUERY_OBJECT_PTR_INTERFACE(TableFrameParameter.pIGameMatchServiceManager,IGameMatchServiceManager);
		if (pIGameMatchServiceManager==NULL)
		{
			ASSERT(FALSE);
			return false;
		}
		if (m_pIGameMatchSink == NULL)
		{
			m_pIGameMatchSink = (IGameMatchSink *)pIGameMatchServiceManager->CreateGameMatchSink(IID_IGameMatchSink, VER_IGameMatchSink);
		}

		//错误判断
		if (m_pIGameMatchSink==NULL)
		{
			ASSERT(FALSE);
			return false;
		}

		//扩展接口
		m_pIMatchTableAction=QUERY_OBJECT_PTR_INTERFACE(m_pIGameMatchSink,ITableUserAction);
		if (m_pIGameMatchSink->InitTableFrameSink(pIUnknownEx)==false) 
		{
			return false;
		}
	}

	return true;
}

//起立动作
bool CTableFrame::PerformStandUpAction(IServerUserItem * pIServerUserItem)
{
	//效验参数
	ASSERT(pIServerUserItem!=NULL);
	ASSERT(pIServerUserItem->GetTableID()==m_wTableID);
	ASSERT(pIServerUserItem->GetChairID()<=m_wChairCount);

	//用户属性
	WORD wChairID=pIServerUserItem->GetChairID();
	BYTE cbUserStatus=pIServerUserItem->GetUserStatus();
	IServerUserItem * pITableUserItem=GetTableUserItem(wChairID);

	// 打印日志
	LOGI("CTableFrame::PerformStandUpAction, UserID="<< pIServerUserItem->GetUserID()<<", NickName=" << pIServerUserItem->GetNickName() << ", cbUserStatus=" << cbUserStatus);

	if (pIServerUserItem == pITableUserItem)
	{
		if (CServerRule::IsAllowOffLineTrustee(m_pGameServiceOption->dwServerRule))
		{
			// 如果玩家是托管退出状态,则直接返回,不进行下面的判断
			// (玩家托管超过次数了,会直接调用当前函数,但是有可能之前的定时器还没被执行,就会重复调用函数,增加以下的判断,消除bug)
			if ((m_bGameStarted == true) && (cbUserStatus == US_TRUSTEE))
			{
				return true;
			}
		}
	}

	//游戏用户
	if ((m_bGameStarted==true)&&((cbUserStatus==US_PLAYING)||(cbUserStatus==US_OFFLINE)))
	{
		if (CServerRule::IsAllowOffLineTrustee(m_pGameServiceOption->dwServerRule))
		{
			// 设置玩家状态是托管状态
			pIServerUserItem->SetUserStatus(US_TRUSTEE, m_wTableID, wChairID);
		}
		//结束游戏
		BYTE cbConcludeReason=(cbUserStatus==US_OFFLINE)?GER_NETWORK_ERROR:GER_USER_LEAVE;
		m_pITableFrameSink->OnEventGameConclude(wChairID,pIServerUserItem,cbConcludeReason);

		//离开判断
		if (m_TableUserItemArray[wChairID] != pIServerUserItem)
		{
			return true;
		}
	}

	//设置变量
	if (pIServerUserItem==pITableUserItem)
	{
		if (CServerRule::IsAllowOffLineTrustee(m_pGameServiceOption->dwServerRule))
		{
			// 允许托管代打
			if ( (m_bGameStarted==true) && pIServerUserItem->GetUserStatus() == US_TRUSTEE)
			{
				// 如果桌上的玩家全都是托退状态，则强制结束本局游戏
				bool bAllTrustee = true;
				for (WORD i = 0; i < m_wChairCount; i++)
				{
					if (GetTableUserItem(i) != NULL)
					{
						if (GetTableUserItem(i)->GetUserStatus() == US_PLAYING || GetTableUserItem(i)->GetUserStatus() == US_OFFLINE)
						{
							bAllTrustee = false;
						}
					}
				}
				if (bAllTrustee && (m_pGameServiceOption->wServerType&GAME_GENRE_USERCUSTOM == 0))
				{
					DismissGame();
				}

				return true;
			}
		}

		// 桌子没有开始,并且不是自建模式,才允许离开
		if ( (m_pGameServiceOption->wServerType&GAME_GENRE_USERCUSTOM) && m_bTableStarted== true)
		{
		}
		else
		{
			//解锁游戏币
			if (m_lFrozenedScore[wChairID] != 0L)
			{
				pIServerUserItem->UnFrozenedUserScore(m_lFrozenedScore[wChairID]);
				m_lFrozenedScore[wChairID] = 0L;
			}

			//事件通知
			if (m_pITableUserAction != NULL)
			{
				m_pITableUserAction->OnActionUserStandUp(wChairID, pIServerUserItem, false);
			}

			//事件通知
			if (m_pIMatchTableAction != NULL) m_pIMatchTableAction->OnActionUserStandUp(wChairID, pIServerUserItem, false);

			//设置变量
			m_TableUserItemArray[wChairID] = NULL;

			//用户状态
			pIServerUserItem->SetClientReady(false);
			pIServerUserItem->SetUserStatus((cbUserStatus == US_OFFLINE) ? US_NULL : US_FREE, INVALID_TABLE, INVALID_CHAIR);

			//变量定义
			bool bTableLocked = IsTableLocked();
			bool bTableStarted = IsTableStarted();
			WORD wTableUserCount = GetSitUserCount();

			//设置变量
			m_wUserCount = wTableUserCount;

			//桌子信息
			if (wTableUserCount == 0)
			{
				m_dwTableOwnerID = 0L;
				m_szEnterPassword[0] = 0;
			}

			//踢走旁观
			if (wTableUserCount == 0)
			{
				for (INT_PTR i = 0; i < m_LookonUserItemArray.GetCount(); i++)
				{
					SendGameMessage(m_LookonUserItemArray[i], TEXT("此游戏桌的所有玩家已经离开了！"), SMT_CLOSE_GAME | SMT_EJECT);
				}
			}

			//结束桌子
			if ((m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM) == 0)
			{
				ConcludeTable();
			}

			//开始判断
			if (EfficacyStartGame(INVALID_CHAIR) == true)
			{
				StartGame();
			}

			//发送状态
			if ((bTableLocked != IsTableLocked()) || (bTableStarted != IsTableStarted()))
			{
				SendTableStatus();
			}
		}

		return true;
	}
	else
	{
		//起立处理
		for (INT_PTR i=0;i<m_LookonUserItemArray.GetCount();i++)
		{
			if (pIServerUserItem==m_LookonUserItemArray[i])
			{
				//删除子项
				m_LookonUserItemArray.RemoveAt(i);

				//事件通知
				if (m_pITableUserAction!=NULL)
				{
					m_pITableUserAction->OnActionUserStandUp(wChairID,pIServerUserItem,true);
				}

				//事件通知
				if(m_pIMatchTableAction!=NULL) m_pIMatchTableAction->OnActionUserStandUp(wChairID,pIServerUserItem,true);

				//用户状态
				pIServerUserItem->SetClientReady(false);
				pIServerUserItem->SetUserStatus(US_FREE,INVALID_TABLE,INVALID_CHAIR);

				return true;
			}
		}

		//错误断言
		ASSERT(FALSE);
	}

	return true;
}

// 托管退出模式下站起
bool CTableFrame::PerformStandUpActionInTrusteeMode(IServerUserItem * pIServerUserItem)
{
	if (pIServerUserItem == NULL)
	{
		return false;
	}
	if (CServerRule::IsAllowOffLineTrustee(m_pGameServiceOption->dwServerRule) && CServerRule::IsAllowTrusteeWriteScore(m_pGameServiceOption->dwServerRule))
	{
		// 如果玩家是托管退出状态,则设置玩家是普通状态
		// 判断玩家是否离开房间了，如果离开了，需要删除玩家
		if (pIServerUserItem->GetUserStatus() == US_TRUSTEE)
		{
			pIServerUserItem->SetUserStatus(US_SIT, m_wTableID, pIServerUserItem->GetChairID());
			PerformStandUpAction(pIServerUserItem);

			// 判断玩家是否离开房间了，如果离开了，需要删除玩家
			if (pIServerUserItem->GetBindIndex() == INVALID_WORD)
			{
				pIServerUserItem->SetUserStatus(US_NULL, INVALID_TABLE, INVALID_CHAIR);
			}

			return true;
		}
	}

	return false;
}

//旁观动作
bool CTableFrame::PerformLookonAction(WORD wChairID, IServerUserItem * pIServerUserItem, LPCTSTR lpszPassword /*=NULL*/)
{
	//效验参数
	ASSERT((pIServerUserItem!=NULL)&&(wChairID<m_wChairCount));
	ASSERT((pIServerUserItem->GetTableID()==INVALID_TABLE)&&(pIServerUserItem->GetChairID()==INVALID_CHAIR));

	//变量定义
	tagUserInfo * pUserInfo=pIServerUserItem->GetUserInfo();
	tagUserRule * pUserRule=pIServerUserItem->GetUserRule();
	IServerUserItem * pITableUserItem=GetTableUserItem(wChairID);

	//游戏状态
	if ((m_bGameStarted==false)&&(pIServerUserItem->GetMasterOrder()==0L))
	{
		SendRequestFailure(pIServerUserItem,TEXT("游戏还没有开始，不能旁观此游戏桌！"),REQUEST_FAILURE_NORMAL);
		return false;
	}

	//模拟处理
	if (m_pGameServiceAttrib->wChairCount < MAX_CHAIR && pIServerUserItem->IsAndroidUser()==false)
	{
		//定义变量
		CAttemperEngineSink * pAttemperEngineSink=(CAttemperEngineSink *)m_pIMainServiceFrame;

		//查找机器
		for (WORD i=0; i<m_pGameServiceAttrib->wChairCount; i++)
		{
			//获取用户
			IServerUserItem *pIUserItem=m_TableUserItemArray[i];
			if(pIUserItem==NULL) continue;
			if(pIUserItem->IsAndroidUser()==false)break;

			//获取参数
			tagBindParameter * pBindParameter=pAttemperEngineSink->GetBindParameter(pIUserItem->GetBindIndex());
			IAndroidUserItem * pIAndroidUserItem=m_pIAndroidUserManager->SearchAndroidUserItem(pIUserItem->GetUserID(),pBindParameter->dwSocketID);
			tagAndroidParameter * pAndroidParameter=pIAndroidUserItem->GetAndroidParameter();

			//模拟判断
			if((pAndroidParameter->dwServiceGender&ANDROID_SIMULATE)!=0
				&& (pAndroidParameter->dwServiceGender&ANDROID_PASSIVITY)==0
				&& (pAndroidParameter->dwServiceGender&ANDROID_INITIATIVE)==0)
			{
				SendRequestFailure(pIServerUserItem,TEXT("抱歉，当前游戏桌子禁止用户旁观！"),REQUEST_FAILURE_NORMAL);
				return false;
			}

			break;
		}
	}


	//旁观判断
	if (CServerRule::IsAllowAndroidSimulate(m_pGameServiceOption->dwServerRule)==true
		&& (CServerRule::IsAllowAndroidAttend(m_pGameServiceOption->dwServerRule)==false))
	{
		if ((pITableUserItem!=NULL)&&(pITableUserItem->IsAndroidUser()==true))
		{
			SendRequestFailure(pIServerUserItem,TEXT("抱歉，当前游戏房间禁止用户旁观！"),REQUEST_FAILURE_NORMAL);
			return false;
		}
	}

	//状态判断
	if ((CServerRule::IsForfendGameLookon(m_pGameServiceOption->dwServerRule)==true)&&(pIServerUserItem->GetMasterOrder()==0))
	{
		SendRequestFailure(pIServerUserItem,TEXT("抱歉，当前游戏房间禁止用户旁观！"),REQUEST_FAILURE_NORMAL);
		return false;
	}

	//椅子判断
	if ((pITableUserItem==NULL)&&(pIServerUserItem->GetMasterOrder()==0L))
	{
		SendRequestFailure(pIServerUserItem,TEXT("您所请求的位置没有游戏玩家，无法旁观此游戏桌"),REQUEST_FAILURE_NORMAL);
		return false;
	}

	//密码效验
// 	if ((IsTableLocked()==true)&&(pIServerUserItem->GetMasterOrder()==0L)&&(lstrcmp(pUserRule->szPassword,m_szEnterPassword)!=0))
// 	{
// 		SendRequestFailure(pIServerUserItem,TEXT("游戏桌进入密码不正确，不能旁观游戏！"),REQUEST_FAILURE_PASSWORD);
// 		return false;
// 	}
	//密码效验
	if((m_pGameServiceOption->wServerType&GAME_GENRE_MATCH)==0 && ((IsTableLocked()==true)&&(pIServerUserItem->GetMasterOrder()==0L))
		&&((lpszPassword==NULL)||(lstrcmp(lpszPassword,m_szEnterPassword)!=0)))
	{
		SendRequestFailure(pIServerUserItem,TEXT("游戏桌进入密码不正确，不能旁观游戏！"),REQUEST_FAILURE_PASSWORD);
		return false;
	}

	//扩展效验
	if (m_pITableUserRequest!=NULL)
	{
		//变量定义
		tagRequestResult RequestResult;
		ZeroMemory(&RequestResult,sizeof(RequestResult));

		//坐下效验
		if (m_pITableUserRequest->OnUserRequestLookon(wChairID,pIServerUserItem,RequestResult)==false)
		{
			//发送信息
			SendRequestFailure(pIServerUserItem,RequestResult.szFailureReason,RequestResult.cbFailureCode);

			return false;
		}
	}

	//设置用户
	m_LookonUserItemArray.Add(pIServerUserItem);

	//用户状态
	pIServerUserItem->SetClientReady(false);
	pIServerUserItem->SetUserStatus(US_LOOKON,m_wTableID,wChairID);

	//事件通知
	if (m_pITableUserAction!=NULL)
	{
		m_pITableUserAction->OnActionUserSitDown(wChairID,pIServerUserItem,true);
	}

	//事件通知
	if(m_pIMatchTableAction!=NULL) m_pIMatchTableAction->OnActionUserSitDown(wChairID,pIServerUserItem,true);
	return true;
}

//坐下动作
bool CTableFrame::PerformSitDownAction(WORD wChairID, IServerUserItem * pIServerUserItem, LPCTSTR lpszPassword)
{
	//效验参数
	ASSERT((pIServerUserItem!=NULL)&&(wChairID<m_wChairCount));
	ASSERT((pIServerUserItem->GetTableID()==INVALID_TABLE)&&(pIServerUserItem->GetChairID()==INVALID_CHAIR));

	//变量定义
	tagUserInfo * pUserInfo=pIServerUserItem->GetUserInfo();
	tagUserRule * pUserRule=pIServerUserItem->GetUserRule();
	IServerUserItem * pITableUserItem=GetTableUserItem(wChairID);

	//积分变量
	SCORE lUserScore=pIServerUserItem->GetUserScore();
	SCORE lMinTableScore = m_pGameTableOption->lMinTableScore;
	SCORE lLessEnterScore=m_pITableFrameSink->QueryLessEnterScore(wChairID,pIServerUserItem);

	//状态判断
	if ((CServerRule::IsForfendGameEnter(m_pGameServiceOption->dwServerRule)==true)&&(pIServerUserItem->GetMasterOrder()==0))
	{
		LOGI("CTableFrame::PerformSitDownAction, dwServerRule="<<m_pGameServiceOption->dwServerRule<<", GetMasterOrder="<<pIServerUserItem->GetMasterOrder())
		SendRequestFailure(pIServerUserItem,TEXT("抱歉，当前游戏桌子禁止用户进入！"),REQUEST_FAILURE_NORMAL);
		return false;
	}

	// 托管代打状态
	if (pIServerUserItem->GetUserStatus() == US_TRUSTEE)
	{
		SendRequestFailure(pIServerUserItem,TEXT("抱歉，您当前处于托管代打状态，不能进入游戏桌！"),REQUEST_FAILURE_NORMAL);
		return false;
	}

	//模拟处理
	if (m_pGameServiceAttrib->wChairCount < MAX_CHAIR && pIServerUserItem->IsAndroidUser()==false)
	{
		//定义变量
		CAttemperEngineSink * pAttemperEngineSink=(CAttemperEngineSink *)m_pIMainServiceFrame;

		//查找机器
		for (WORD i=0; i<m_pGameServiceAttrib->wChairCount; i++)
		{
			//获取用户
			IServerUserItem *pIUserItem=m_TableUserItemArray[i];
			if(pIUserItem==NULL) continue;
			if(pIUserItem->IsAndroidUser()==false)break;

			//获取参数
			tagBindParameter * pBindParameter=pAttemperEngineSink->GetBindParameter(pIUserItem->GetBindIndex());
			IAndroidUserItem * pIAndroidUserItem=m_pIAndroidUserManager->SearchAndroidUserItem(pIUserItem->GetUserID(),pBindParameter->dwSocketID);
			tagAndroidParameter * pAndroidParameter=pIAndroidUserItem->GetAndroidParameter();

			//模拟判断
			if((pAndroidParameter->dwServiceGender&ANDROID_SIMULATE)!=0
				&& (pAndroidParameter->dwServiceGender&ANDROID_PASSIVITY)==0
				&& (pAndroidParameter->dwServiceGender&ANDROID_INITIATIVE)==0)
			{
				LOGI("CTableFrame::PerformSitDownAction, dwServiceGender="<<pAndroidParameter->dwServiceGender);
				SendRequestFailure(pIServerUserItem,TEXT("抱歉，当前游戏桌子禁止用户进入！"),REQUEST_FAILURE_NORMAL);
				return false;
			}

			break;
		}
	}

	//动态加入
	bool bDynamicJoin=true;
	if (m_pGameServiceAttrib->cbDynamicJoin==FALSE) bDynamicJoin=false;
	if (CServerRule::IsAllowDynamicJoin(m_pGameServiceOption->dwServerRule)==false) bDynamicJoin=false;

	//游戏状态
	if ((m_bGameStarted==true)&&(bDynamicJoin==false))
	{
		SendRequestFailure(pIServerUserItem,TEXT("游戏已经开始了，现在不能进入游戏桌！"),REQUEST_FAILURE_NORMAL);
		return false;
	}

	//椅子判断
	if (pITableUserItem!=NULL)
	{
		//防作弊
		if(CServerRule::IsAllowAvertCheatMode(m_pGameServiceOption->dwServerRule)) return false;

		//构造信息
		TCHAR szDescribe[128]=TEXT("");
		_sntprintf(szDescribe,CountArray(szDescribe),TEXT("椅子已经被 [ %s ] 捷足先登了，下次动作要快点了！"),pITableUserItem->GetNickName());

		//发送信息
		SendRequestFailure(pIServerUserItem,szDescribe,REQUEST_FAILURE_NORMAL);

		return false;
	}

	//密码效验
	if((m_pGameServiceOption->wServerType&GAME_GENRE_MATCH)==0 && ((IsTableLocked()==true)&&(pIServerUserItem->GetMasterOrder()==0L))
		&&((lpszPassword==NULL)||(lstrcmp(lpszPassword,m_szEnterPassword)!=0)))
	{
		SendRequestFailure(pIServerUserItem,TEXT("游戏桌进入密码不正确，不能加入游戏！"),REQUEST_FAILURE_PASSWORD);
		return false;
	}

	//规则效验
	if ((m_pGameServiceOption->wServerType&GAME_GENRE_MATCH) == 0 && (m_pGameServiceOption->wServerType&GAME_GENRE_USERCUSTOM) == 0)
	{
		if (EfficacyEnterTableScoreRule(wChairID, pIServerUserItem) == false)
		{
			return false;
		}
		if (EfficacyEnterTableMemberRule(pIServerUserItem) == false)
		{
			return false;
		}
		if (EfficacyIPAddress(pIServerUserItem) == false)
		{
			return false;
		}
		if (EfficacyScoreRule(pIServerUserItem) == false)
		{
			return false;
		}
	}

	//扩展效验
	if ((m_pGameServiceOption->wServerType&GAME_GENRE_MATCH)==0 && m_pITableUserRequest!=NULL)
	{
		//变量定义
		tagRequestResult RequestResult;
		ZeroMemory(&RequestResult,sizeof(RequestResult));

		//坐下效验
		if (m_pITableUserRequest->OnUserRequestSitDown(wChairID,pIServerUserItem,RequestResult)==false)
		{
			//发送信息
			SendRequestFailure(pIServerUserItem,RequestResult.szFailureReason,RequestResult.cbFailureCode);

			return false;
		}
	}

	//设置变量
	m_TableUserItemArray[wChairID]=pIServerUserItem;
	if ( (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM) == 0)
	{
		// 非自建模式,有玩家坐下时,重置局数
		m_wDrawCount = 0;
	}

	//用户状态
	if ((IsGameStarted()==false)||(m_cbStartMode!=START_MODE_TIME_CONTROL))
	{
		if (CServerRule::IsAllowAvertCheatMode(m_pGameServiceOption->dwServerRule)==false && (m_pGameServiceOption->wServerType&GAME_GENRE_MATCH)==0)
		{
			pIServerUserItem->SetClientReady(false);
			pIServerUserItem->SetUserStatus(US_SIT,m_wTableID,wChairID);
		}
		else
		{
			pIServerUserItem->SetClientReady(false);
			pIServerUserItem->SetUserStatus(US_READY,m_wTableID,wChairID);
		}
	}
	else
	{
		//设置变量
		m_wOffLineCount[wChairID]=0L;
		m_dwOffLineTime[wChairID]=0L;

		//锁定游戏币
		if (m_pGameTableOption->lServiceScore > 0L)
		{
			m_lFrozenedScore[wChairID] = m_pGameTableOption->lServiceScore;
			pIServerUserItem->FrozenedUserScore(m_pGameTableOption->lServiceScore);
		}

		//设置状态
		pIServerUserItem->SetClientReady(false);
		pIServerUserItem->SetUserStatus(US_PLAYING,m_wTableID,wChairID);
	}

	// 设定玩家限制货币
	pIServerUserItem->SetRestrictScore(m_pGameTableOption->lRestrictScore);
	
	//设置变量
	m_wUserCount=GetSitUserCount();

	//桌子信息
	if (GetSitUserCount()==1)
	{
		//状态变量
		bool bTableLocked=IsTableLocked();

		//设置变量
		m_dwTableOwnerID=pIServerUserItem->GetUserID();
		lstrcpyn(m_szEnterPassword,pUserRule->szPassword,CountArray(m_szEnterPassword));

		//发送状态
		if (bTableLocked!=IsTableLocked()) SendTableStatus();
	}

	//事件通知
	if (m_pITableUserAction!=NULL)
	{
		m_pITableUserAction->OnActionUserSitDown(wChairID,pIServerUserItem,false);
	}

	//事件通知
	if(m_pIMatchTableAction!=NULL) m_pIMatchTableAction->OnActionUserSitDown(wChairID,pIServerUserItem,false);


	return true;
}

//桌子状态
bool CTableFrame::SendTableStatus()
{
	//变量定义
	CMD_GR_TableStatus TableStatus;
	ZeroMemory(&TableStatus,sizeof(TableStatus));

	//构造数据
	TableStatus.wTableID=m_wTableID;
	TableStatus.TableStatus.cbTableLock=IsTableLocked()?TRUE:FALSE;
	TableStatus.TableStatus.cbPlayStatus=IsTableStarted()?TRUE:FALSE;

	//电脑数据
	m_pIMainServiceFrame->SendData(BG_COMPUTER,MDM_GR_STATUS,SUB_GR_TABLE_STATUS,&TableStatus,sizeof(TableStatus));

	//手机数据

	// 发送自建桌子相关信息
	if (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM)
	{
		CMD_GF_CustomTableInfo CustomTableInfo;
		CustomTableInfo.dwUserID = m_pGameTableOption->dwCreatUserID;
		CustomTableInfo.bTableStarted = m_bTableStarted;
		CustomTableInfo.dwRoundCount = m_pGameTableOption->dwRoundCount;
		CustomTableInfo.dwPlayRoundCount = m_wDrawCount;
		lstrcpyn(CustomTableInfo.szSecret, m_pGameTableOption->szSecret, sizeof(CustomTableInfo));
		for (WORD i = 0; i < GetChairCount(); i++)
		{
			SendTableData(i, SUB_GF_CUSTOMTABLE_INFO, &CustomTableInfo, sizeof(CustomTableInfo), MDM_GF_FRAME);
		}
	}

	return true;
}

//请求失败
bool CTableFrame::SendRequestFailure(IServerUserItem * pIServerUserItem, LPCTSTR pszDescribe, LONG lErrorCode)
{
	//变量定义
	CMD_GR_RequestFailure RequestFailure;
	ZeroMemory(&RequestFailure,sizeof(RequestFailure));

	//构造数据
	RequestFailure.lErrorCode=lErrorCode;
	lstrcpyn(RequestFailure.szDescribeString,pszDescribe,CountArray(RequestFailure.szDescribeString));

	//发送数据
	WORD wDataSize=CountStringBuffer(RequestFailure.szDescribeString);
	WORD wHeadSize=sizeof(RequestFailure)-sizeof(RequestFailure.szDescribeString);
	m_pIMainServiceFrame->SendData(pIServerUserItem,MDM_GR_USER,SUB_GR_REQUEST_FAILURE,&RequestFailure,wHeadSize+wDataSize);

	return true;
}

//开始效验
bool CTableFrame::EfficacyStartGame(WORD wReadyChairID)
{
	//状态判断
	if (m_bGameStarted==true) return false;

	//模式过滤
	if (m_cbStartMode==START_MODE_TIME_CONTROL) return false;
	if (m_cbStartMode==START_MODE_MASTER_CONTROL) return false;

	//准备人数
	WORD wReadyUserCount=0;
	for (WORD i=0;i<m_wChairCount;i++)
	{
		//获取用户
		IServerUserItem * pITableUserItem=GetTableUserItem(i);
		if (pITableUserItem==NULL) continue;

		//用户统计
		if (pITableUserItem!=NULL)
		{
			//状态判断
			if (pITableUserItem->IsClientReady()==false) return false;
			if ((wReadyChairID!=i)&&(pITableUserItem->GetUserStatus()!=US_READY)) return false;

			//用户计数
			wReadyUserCount++;
		}
	}

	//开始处理
	switch (m_cbStartMode)
	{
	case START_MODE_ALL_READY:			//所有准备
		{
			//数目判断
			if (wReadyUserCount>=2L) return true;

			return false;
		}
	case START_MODE_FULL_READY:			//满人开始
		{
			//人数判断
			if (wReadyUserCount==m_wChairCount) return true;

			return false;
		}
	case START_MODE_PAIR_READY:			//配对开始
		{
			//数目判断
			if (wReadyUserCount==m_wChairCount) return true;
			if ((wReadyUserCount<2L)||(wReadyUserCount%2)!=0) return false;

			//位置判断
			for (WORD i=0;i<m_wChairCount/2;i++)
			{
				//获取用户
				IServerUserItem * pICurrentUserItem=GetTableUserItem(i);
				IServerUserItem * pITowardsUserItem=GetTableUserItem(i+m_wChairCount/2);

				//位置过滤
				if ((pICurrentUserItem==NULL)&&(pITowardsUserItem!=NULL)) return false;
				if ((pICurrentUserItem!=NULL)&&(pITowardsUserItem==NULL)) return false;
			}

			return true;
		}
	default:
		{
			ASSERT(FALSE);
			return false;
		}
	}

	return false;
}

//地址效验
bool CTableFrame::EfficacyIPAddress(IServerUserItem * pIServerUserItem)
{
	//管理员不受限制
	if(pIServerUserItem->GetMasterOrder()!=0) return true;

	//规则判断
	if (CServerRule::IsForfendGameRule(m_pGameServiceOption->dwServerRule)==true) return true;

	//防作弊
	if(CServerRule::IsAllowAvertCheatMode(m_pGameServiceOption->dwServerRule)) return true;

	//地址效验
	const tagUserRule * pUserRule=pIServerUserItem->GetUserRule(),*pTableUserRule=NULL;
	bool bCheckSameIP=pUserRule->bLimitSameIP;
	for (WORD i=0;i<m_wChairCount;i++)
	{
		//获取用户
		IServerUserItem * pITableUserItem=GetTableUserItem(i);
		if (pITableUserItem!=NULL && (!pITableUserItem->IsAndroidUser()) && (pITableUserItem->GetMasterOrder()==0))
		{
			pTableUserRule=pITableUserItem->GetUserRule();
			if (pTableUserRule->bLimitSameIP==true) 
			{
				bCheckSameIP=true;
				break;
			}
		}
	}

	//地址效验
	if (bCheckSameIP==true)
	{
		DWORD dwUserIP=pIServerUserItem->GetClientAddr();
		for (WORD i=0;i<m_wChairCount;i++)
		{
			//获取用户
			IServerUserItem * pITableUserItem=GetTableUserItem(i);
			if ((pITableUserItem!=NULL)&&(pITableUserItem != pIServerUserItem)&&(!pITableUserItem->IsAndroidUser())&&(pITableUserItem->GetMasterOrder()==0)&&(pITableUserItem->GetClientAddr()==dwUserIP))
			{
				if (!pUserRule->bLimitSameIP)
				{
					//发送信息
					LPCTSTR pszDescribe=TEXT("此游戏桌玩家设置了不跟相同 IP 地址的玩家游戏，您 IP 地址与此玩家的 IP 地址相同，不能加入游戏！");
					SendRequestFailure(pIServerUserItem,pszDescribe,REQUEST_FAILURE_NORMAL);
					return false;
				}
				else
				{
					//发送信息
					LPCTSTR pszDescribe=TEXT("您设置了不跟相同 IP 地址的玩家游戏，此游戏桌存在与您 IP 地址相同的玩家，不能加入游戏！");
					SendRequestFailure(pIServerUserItem,pszDescribe,REQUEST_FAILURE_NORMAL);
					return false;
				}
			}
		}
		for (WORD i=0;i<m_wChairCount-1;i++)
		{
			//获取用户
			IServerUserItem * pITableUserItem=GetTableUserItem(i);
			if (pITableUserItem!=NULL && (!pITableUserItem->IsAndroidUser()) && (pITableUserItem->GetMasterOrder()==0))
			{
				for (WORD j=i+1;j<m_wChairCount;j++)
				{
					//获取用户
					IServerUserItem * pITableNextUserItem=GetTableUserItem(j);
					if ((pITableNextUserItem!=NULL) && (!pITableNextUserItem->IsAndroidUser()) && (pITableNextUserItem->GetMasterOrder()==0)&&(pITableUserItem->GetClientAddr()==pITableNextUserItem->GetClientAddr()))
					{
						LPCTSTR pszDescribe=TEXT("您设置了不跟相同 IP 地址的玩家游戏，此游戏桌存在 IP 地址相同的玩家，不能加入游戏！");
						SendRequestFailure(pIServerUserItem,pszDescribe,REQUEST_FAILURE_NORMAL);
						return false;
					}
				}
			}
		}
	}

	return true;
}

// 地址校验(用于自建桌子校验)
bool CTableFrame::EfficacyIPAddress()
{
	// 检查进来的玩家是否和桌上的玩家IP相同,相同的话告诉其它玩家
	map<DWORD, vector<DWORD>> mapUserIPInfo;		// 玩家IP信息(map<IP, vector<User>>)
	for (WORD i = 0; i < m_wChairCount; i++)
	{
		//获取用户
		IServerUserItem * pITableUserItem = GetTableUserItem(i);
		if (pITableUserItem == nullptr || pITableUserItem->IsAndroidUser() || pITableUserItem->GetMasterOrder() != 0)
		{
			continue;
		}

		auto iter = mapUserIPInfo.find(pITableUserItem->GetClientAddr());
		if (iter != mapUserIPInfo.end())
		{
			iter->second.push_back(pITableUserItem->GetUserID());
		}
		else
		{
			vector<DWORD> vectUserID;
			vectUserID.push_back(pITableUserItem->GetUserID());
			mapUserIPInfo.insert(make_pair(pITableUserItem->GetClientAddr(), vectUserID));
		}
	}

	for (auto &iter : mapUserIPInfo)
	{
		if (iter.second.size() > 1)
		{
			// 至少两个玩家IP相同
			TCHAR szMessage[256] = { 0 };
			_sntprintf(szMessage, sizeof(szMessage), TEXT("玩家"));
			for (size_t i = 0; i < iter.second.size(); i++)
			{
				IServerUserItem* pITableUserItem = SearchUserItem(iter.second.at(i));
				if (pITableUserItem)
				{
					if (i == iter.second.size() - 1)
					{
						_sntprintf(szMessage, sizeof(szMessage), TEXT("%s[%s]"), szMessage, pITableUserItem->GetNickName());
					}
					else
					{
						_sntprintf(szMessage, sizeof(szMessage), TEXT("%s[%s],"), szMessage, pITableUserItem->GetNickName());
					}
				}
			}
			_sntprintf(szMessage, sizeof(szMessage), TEXT("%s为同一IP地址"), szMessage);

			SendGameMessage(szMessage, SMT_CHAT | SMT_EJECT);
		}
	}
	return true;
}

//积分效验
bool CTableFrame::EfficacyScoreRule(IServerUserItem * pIServerUserItem)
{
	//管理员不受限制
	if(pIServerUserItem->GetMasterOrder()!=0) return true;

	//规则判断
	if (CServerRule::IsForfendGameRule(m_pGameServiceOption->dwServerRule)==true) return true;

	//防作弊
	if(CServerRule::IsAllowAvertCheatMode(m_pGameServiceOption->dwServerRule)) return true;

	//变量定义
	WORD wWinRate=pIServerUserItem->GetUserWinRate();
	WORD wFleeRate=pIServerUserItem->GetUserFleeRate();

	//积分范围
	for (WORD i=0;i<m_wChairCount;i++)
	{
		//获取用户
		IServerUserItem * pITableUserItem=GetTableUserItem(i);

		//规则效验
		if (pITableUserItem!=NULL)
		{
			//获取规则
			tagUserRule * pTableUserRule=pITableUserItem->GetUserRule();

			//逃率效验
			if ((pTableUserRule->bLimitFleeRate)&&(wFleeRate>pTableUserRule->wMaxFleeRate))
			{
				//构造信息
				TCHAR szDescribe[128]=TEXT("");
				_sntprintf(szDescribe,CountArray(szDescribe),TEXT("您的逃跑率太高，与 %s 设置的设置不符，不能加入游戏！"),pITableUserItem->GetNickName());

				//发送信息
				SendRequestFailure(pIServerUserItem,szDescribe,REQUEST_FAILURE_NORMAL);

				return false;
			}

			//胜率效验
			if ((pTableUserRule->bLimitWinRate)&&(wWinRate<pTableUserRule->wMinWinRate))
			{
				//构造信息
				TCHAR szDescribe[128]=TEXT("");
				_sntprintf(szDescribe,CountArray(szDescribe),TEXT("您的胜率太低，与 %s 设置的设置不符，不能加入游戏！"),pITableUserItem->GetNickName());

				//发送信息
				SendRequestFailure(pIServerUserItem,szDescribe,REQUEST_FAILURE_NORMAL);

				return false;
			}

			//积分效验
			if (pTableUserRule->bLimitGameScore==true)
			{
				//最高积分
				if (pIServerUserItem->GetUserScore()>pTableUserRule->lMaxGameScore)
				{
					//构造信息
					TCHAR szDescribe[128]=TEXT("");
					_sntprintf(szDescribe,CountArray(szDescribe),TEXT("您的积分太高，与 %s 设置的设置不符，不能加入游戏！"),pITableUserItem->GetNickName());

					//发送信息
					SendRequestFailure(pIServerUserItem,szDescribe,REQUEST_FAILURE_NORMAL);

					return false;
				}

				//最低积分
				if (pIServerUserItem->GetUserScore()<pTableUserRule->lMinGameScore)
				{
					//构造信息
					TCHAR szDescribe[128]=TEXT("");
					_sntprintf(szDescribe,CountArray(szDescribe),TEXT("您的积分太低，与 %s 设置的设置不符，不能加入游戏！"),pITableUserItem->GetNickName());

					//发送信息
					SendRequestFailure(pIServerUserItem,szDescribe,REQUEST_FAILURE_NORMAL);

					return false;
				}
			}
		}
	}

	return true;
}

//积分效验
bool CTableFrame::EfficacyEnterTableScoreRule(WORD wChairID, IServerUserItem * pIServerUserItem)
{
	//积分变量
	SCORE lUserScore=pIServerUserItem->GetUserScore();
	SCORE lMinTableScore = m_pGameTableOption->lMinTableScore;
	SCORE lLessEnterScore=m_pITableFrameSink->QueryLessEnterScore(wChairID,pIServerUserItem);

	if (((lMinTableScore!=0L)&&(lUserScore<lMinTableScore))||((lLessEnterScore!=0L)&&(lUserScore<lLessEnterScore)))
	{
		//构造信息
		TCHAR szDescribe[128]=TEXT("");
		if(m_pGameServiceOption->wServerType==GAME_GENRE_GOLD)
			_sntprintf(szDescribe,CountArray(szDescribe),TEXT("加入游戏至少需要 ") SCORE_STRING TEXT(" 的游戏币，您的游戏币不够，不能加入！"),__max(lMinTableScore,lLessEnterScore));
		else if(m_pGameServiceOption->wServerType==GAME_GENRE_MATCH)
			_sntprintf(szDescribe,CountArray(szDescribe),TEXT("加入游戏至少需要 ") SCORE_STRING TEXT(" 的比赛币，您的比赛币不够，不能加入！"),__max(lMinTableScore,lLessEnterScore));
		else
			_sntprintf(szDescribe,CountArray(szDescribe),TEXT("加入游戏至少需要 ") SCORE_STRING TEXT(" 的游戏积分，您的积分不够，不能加入！"),__max(lMinTableScore,lLessEnterScore));

		//发送信息
		SendRequestFailure(pIServerUserItem,szDescribe,REQUEST_FAILURE_NOSCORE);

		return false;
	}

//	SCORE lMaxTableScore = m_pGameServiceOption->TableOption[m_wTableID].lMaxTableScore;
	SCORE lMaxTableScore = m_pGameTableOption->lMaxTableScore;
	if ((lMaxTableScore != 0) && lUserScore > lMaxTableScore)
	{
		//构造信息
		TCHAR szDescribe[128] = TEXT("");
		if (m_pGameServiceOption->wServerType == GAME_GENRE_GOLD)
			_sntprintf(szDescribe, CountArray(szDescribe), TEXT("加入游戏至多不能超过 ") SCORE_STRING TEXT(" 的游戏币，您的游戏币不符合配置，不能加入！"), lMaxTableScore);
		else if (m_pGameServiceOption->wServerType == GAME_GENRE_MATCH)
			_sntprintf(szDescribe, CountArray(szDescribe), TEXT("加入游戏至多不能超过 ") SCORE_STRING TEXT(" 的比赛币，您的比赛币不符合配置，不能加入！"), lMaxTableScore);
		else
			_sntprintf(szDescribe, CountArray(szDescribe), TEXT("加入游戏至多不能超过 ") SCORE_STRING TEXT(" 的游戏积分，您的积分不符合配置，不能加入！"), lMaxTableScore);

		//发送信息
		SendRequestFailure(pIServerUserItem, szDescribe, REQUEST_FAILURE_NOSCORE);
		return false;
	}

	return true;
}

// 权限校验
bool CTableFrame::EfficacyEnterTableMemberRule(IServerUserItem * pIServerUserItem)
{
	BYTE cbMemberOrder = pIServerUserItem->GetMemberOrder();
	BYTE cbMasterOrder = pIServerUserItem->GetMasterOrder();
	//会员判断
	if (m_pGameTableOption->cbMinTableEnterMember != 0 && cbMemberOrder < m_pGameTableOption->cbMinTableEnterMember && (cbMemberOrder == 0))
	{
		//发送失败
		TCHAR szDescribe[128] = TEXT("");
		_sntprintf(szDescribe, CountArray(szDescribe), TEXT("抱歉，您的会员级别低于当前桌子最低坐下会员条件，不能坐下！"));

		//解除锁定
		SendRequestFailure(pIServerUserItem, szDescribe, REQUEST_FAILURE_NOSCORE);
	
		return false;
	}

	//会员判断
	if (m_pGameTableOption->cbMinTableEnterMember != 0 && cbMemberOrder > m_pGameTableOption->cbMinTableEnterMember && (cbMasterOrder == 0))
	{
		//发送失败
		TCHAR szDescribe[128] = TEXT("");
		_sntprintf(szDescribe, CountArray(szDescribe), TEXT("抱歉，您的会员级别高于当前桌子坐下最高会员条件，不能坐下！"));

		//解除锁定
		SendRequestFailure(pIServerUserItem, szDescribe, REQUEST_FAILURE_NOSCORE);

		return false;
	}
	return true;
}

bool CTableFrame::SetMatchInterface(IUnknownEx * pIUnknownEx)
{
	return NULL;
}

//检查分配
bool CTableFrame::CheckDistribute()
{
	//防作弊
	if(CServerRule::IsAllowAvertCheatMode(m_pGameServiceOption->dwServerRule))
	{
		//桌子状况
		tagTableUserInfo TableUserInfo;
		WORD wUserSitCount=GetTableUserInfo(TableUserInfo);

		//用户起立
		if(wUserSitCount < TableUserInfo.wMinUserCount)
		{
			return true;
		}
	}

	return false;
}

//游戏记录
void CTableFrame::RecordGameScore(bool bDrawStarted)
{
	if (bDrawStarted==true)
	{
		//写入记录
		if (CServerRule::IsRecordGameScore(m_pGameServiceOption->dwServerRule)==true)
		{
			LOGI("CTableFrame::RecordGameScore");
			//变量定义
			DBR_GR_GameScoreRecord GameScoreRecord;
			ZeroMemory(&GameScoreRecord,sizeof(GameScoreRecord));

			//设置变量
			GameScoreRecord.wTableID=m_wTableID;
			GameScoreRecord.dwPlayTimeCount=(bDrawStarted==true)?(DWORD)time(NULL)-m_dwDrawStartTime:0;

			//游戏时间
			GameScoreRecord.SystemTimeStart=m_SystemTimeStart;
			GetLocalTime(&GameScoreRecord.SystemTimeConclude);

			//用户积分
			for (INT_PTR i=0;i<m_GameScoreRecordActive.GetCount();i++)
			{
				//获取对象
				ASSERT(m_GameScoreRecordActive[i]!=NULL);
				tagGameScoreRecord * pGameScoreRecord=m_GameScoreRecordActive[i];

				//用户数目
				if (pGameScoreRecord->cbAndroid==FALSE)
				{
					GameScoreRecord.wUserCount++;
				}
				else
				{
					GameScoreRecord.wAndroidCount++;
				}

				//奖牌统计
				GameScoreRecord.dwUserMemal+=pGameScoreRecord->dwUserMemal;

				//统计信息
				if (pGameScoreRecord->cbAndroid==FALSE)
				{
					GameScoreRecord.lWasteCount-=(pGameScoreRecord->lScore+pGameScoreRecord->lRevenue);
					GameScoreRecord.lRevenueCount+=pGameScoreRecord->lRevenue;
				}

				//游戏币信息
				if (GameScoreRecord.wRecordCount<CountArray(GameScoreRecord.GameScoreRecord))
				{
					WORD wIndex=GameScoreRecord.wRecordCount++;
					CopyMemory(&GameScoreRecord.GameScoreRecord[wIndex],pGameScoreRecord,sizeof(tagGameScoreRecord));
				}
			}

			//投递数据
			if(GameScoreRecord.wUserCount > 0)
			{
				WORD wHeadSize=sizeof(GameScoreRecord)-sizeof(GameScoreRecord.GameScoreRecord);
				WORD wDataSize=sizeof(GameScoreRecord.GameScoreRecord[0])*GameScoreRecord.wRecordCount;
				m_pIRecordDataBaseEngine->PostDataBaseRequest(DBR_GR_GAME_SCORE_RECORD,0,&GameScoreRecord,wHeadSize+wDataSize);
			}
		}

		//清理记录
		if (m_GameScoreRecordActive.GetCount()>0L)
		{
			m_GameScoreRecordBuffer.Append(m_GameScoreRecordActive);
			m_GameScoreRecordActive.RemoveAll();
		}
	}

}
//////////////////////////////////////////////////////////////////////////////////
