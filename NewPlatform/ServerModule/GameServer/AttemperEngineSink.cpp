#include "StdAfx.h"
#include "ServiceUnits.h"
#include "ControlPacket.h"
#include "AttemperEngineSink.h"

#include <math.h>
#include <algorithm>
#include <vector>
using namespace std;

// 打印内存
#ifdef _DEBUG

#include <psapi.h>  
#pragma comment(lib,"psapi.lib")  

#endif // _DEBUG

//////////////////////////////////////////////////////////////////////////////////
//时间标识

#define IDI_LOAD_ANDROID_USER		(IDI_MAIN_MODULE_START+1)			//机器信息
#define IDI_REPORT_SERVER_INFO		(IDI_MAIN_MODULE_START+2)			//房间信息
#define IDI_CONNECT_CORRESPOND		(IDI_MAIN_MODULE_START+3)			//连接时间
#define IDI_GAME_SERVICE_PULSE		(IDI_MAIN_MODULE_START+4)			//服务脉冲
#define IDI_DISTRIBUTE_ANDROID		(IDI_MAIN_MODULE_START+5)			//分配机器
#define IDI_DBCORRESPOND_NOTIFY		(IDI_MAIN_MODULE_START+6)			//缓存通知
#define IDI_LOAD_SYSTEM_MESSAGE		(IDI_MAIN_MODULE_START+7)			//系统消息
#define IDI_SEND_SYSTEM_MESSAGE		(IDI_MAIN_MODULE_START+8)			//系统消息
#define IDI_LOAD_SENSITIVE_WORD		(IDI_MAIN_MODULE_START+9)			//加载敏感词
#define IDI_DISTRIBUTE_USER		    (IDI_MAIN_MODULE_START+10)			//分配用户
#define IDI_ON_ANDROID_SEND_TRUMPT  (IDI_MAIN_MODULE_START+11)
#define IDI_LOAD_BALANCE_SCORE_CURVE (IDI_MAIN_MODULE_START+12)			//加载平衡分曲线
#define IDI_SQUARE_MATCH_ONTIMER	(IDI_MAIN_MODULE_START+13)			//广场赛计时器

//////////////////////////////////////////////////////////////////////////////////
//时间定义 秒

#define TIME_LOAD_ANDROID_USER		600L								//加载机器
#define TIME_DISTRIBUTE_ANDROID		2L									//分配用户
#define TIME_REPORT_SERVER_INFO		30L									//上报时间
#define TIME_DBCORRESPOND_NOTIFY	3L									//缓存通知时间
#define TIME_LOAD_SYSTEM_MESSAGE	900L								//系统消息时间
#define TIME_SEND_SYSTEM_MESSAGE	10L								    //系统消息时间
#define TIME_LOAD_SENSITIVE_WORD	5L									//加载敏感词时间
#define TIME_LOAD_BALANCE_SCORE_CURVE	3L								// 加载平衡分曲线

//////////////////////////////////////////////////////////////////////////////////

//构造函数
CAttemperEngineSink::CAttemperEngineSink()
{
	//状态变量
	m_bCollectUser=false;
	m_bNeekCorrespond=true;
	m_nAndroidUserMessageCount=0;
	m_nAndroidUserSendedCount=0;

	//绑定数据
	m_pNormalParameter=NULL;
	m_pAndroidParameter=NULL;

	//状态变量
	m_pInitParameter=NULL;
	m_pGameParameter=NULL;
	m_pGameServiceAttrib=NULL;
	m_pGameServiceOption=NULL;
	m_pGameServiceTableOption = NULL;
	m_cbGameSquareOptionArrayCount=0;
// 	for(BYTE i=0; i<20; i++)memset(&m_GameSquareOptionArray[i],0,sizeof(m_GameSquareOptionArray[i]));

	//组件变量
	m_pITimerEngine=NULL;
	m_pIAttemperEngine=NULL;
	m_pITCPSocketService=NULL;
	m_pITCPNetworkEngine=NULL;
	m_pIGameServiceManager=NULL;
	m_WaitDistributeList.RemoveAll();

	//数据引擎
	m_pIRecordDataBaseEngine=NULL;
	m_pIKernelDataBaseEngine=NULL;
	m_pIDBCorrespondManager=NULL;

	//配置数据
	ZeroMemory(&m_DataConfigColumn,sizeof(m_DataConfigColumn));
	ZeroMemory(&m_DataConfigProperty,sizeof(m_DataConfigProperty));
	for(int i=0; i<20; i++)m_pGameSquareOptionArray[i]=NULL;

	//比赛变量
	m_pIGameMatchServiceManager=NULL;

	return;
}

//析构函数
CAttemperEngineSink::~CAttemperEngineSink()
{
	//删除数据
	SafeDeleteArray(m_pNormalParameter);
	SafeDeleteArray(m_pAndroidParameter);

	//删除桌子
	for (size_t i=0;i<m_TableFrameArray.size();i++)
	{
		SafeRelease(m_TableFrameArray[i]);
	}
	m_TableFrameArray.clear();

	//清理数据
	m_KickUserItemMap.RemoveAll();
	ClearSystemMessageData();
	m_WaitDistributeList.RemoveAll();

	return;
}

//接口查询
VOID * CAttemperEngineSink::QueryInterface(REFGUID Guid, DWORD dwQueryVer)
{
	QUERYINTERFACE(IMainServiceFrame,Guid,dwQueryVer);
	QUERYINTERFACE(IAttemperEngineSink,Guid,dwQueryVer);
	QUERYINTERFACE(IServerUserItemSink,Guid,dwQueryVer);
	QUERYINTERFACE_IUNKNOWNEX(IAttemperEngineSink,Guid,dwQueryVer);
	return NULL;
}

//启动事件
bool CAttemperEngineSink::OnAttemperEngineStart(IUnknownEx * pIUnknownEx)
{
	//绑定信息
	m_pAndroidParameter=new tagBindParameter[MAX_ANDROID];
	ZeroMemory(m_pAndroidParameter,sizeof(tagBindParameter)*MAX_ANDROID);

	//绑定信息
	m_pNormalParameter=new tagBindParameter[m_pGameServiceOption->wMaxPlayer];
	ZeroMemory(m_pNormalParameter,sizeof(tagBindParameter)*m_pGameServiceOption->wMaxPlayer);

	//配置机器
	if (InitAndroidUser()==false)
	{
		ASSERT(FALSE);
		return false;
	}

	//配置桌子
	if (InitTableFrameArray()==false)
	{
		ASSERT(FALSE);
		return false;
	}

	//配置广场赛
	if (InitSquareMatch()==false)
	{
		ASSERT(FALSE);
		return false;
	}

	//设置接口
	if (m_ServerUserManager.SetServerUserItemSink(QUERY_ME_INTERFACE(IServerUserItemSink))==false)
	{
		ASSERT(FALSE);
		return false;
	}

	//启动机器
	if (m_AndroidUserManager.StartService()==false)
	{
		ASSERT(FALSE);
		return false;
	}

	//获取路径
	TCHAR szWorkDir[MAX_PATH]=TEXT("");
	CWHService::GetWorkDirectory(szWorkDir,CountArray(szWorkDir));

	//构造路径
	TCHAR szIniFile[MAX_PATH]=TEXT("");
	_sntprintf(szIniFile,CountArray(szIniFile),TEXT("%s\\AndroidMessageConfig.ini"),szWorkDir);

	//读取配置
	CWHIniData IniData;
	IniData.SetIniFilePath(szIniFile);
	
	int nAndroidMessageTimeCount=10000; 
	nAndroidMessageTimeCount=IniData.ReadInt(TEXT("Time"),TEXT("Elapse"),10000);
	m_nAndroidUserMessageCount=IniData.ReadInt(TEXT("MessageCount"),TEXT("Count"),0);
	m_pITimerEngine->SetTimer(IDI_ON_ANDROID_SEND_TRUMPT,nAndroidMessageTimeCount*1000,TIMES_INFINITY,NULL);


	//设置时间
	m_pITimerEngine->SetTimer(IDI_GAME_SERVICE_PULSE,1000L,TIMES_INFINITY,NULL);

#ifdef _DEBUG
	m_pITimerEngine->SetTimer(IDI_LOAD_ANDROID_USER,10000L,TIMES_INFINITY,NULL);
#else
	m_pITimerEngine->SetTimer(IDI_LOAD_ANDROID_USER,TIME_LOAD_ANDROID_USER*1000L,TIMES_INFINITY,NULL);
#endif

	m_pITimerEngine->SetTimer(IDI_DISTRIBUTE_ANDROID,TIME_DISTRIBUTE_ANDROID*1000L,TIMES_INFINITY,NULL);

	m_pITimerEngine->SetTimer(IDI_DBCORRESPOND_NOTIFY,TIME_DBCORRESPOND_NOTIFY*1000L,TIMES_INFINITY,NULL);

#ifdef _DEBUG
	m_pITimerEngine->SetTimer(IDI_LOAD_SYSTEM_MESSAGE,15*1000L,TIMES_INFINITY,NULL);
	m_pITimerEngine->SetTimer(IDI_SEND_SYSTEM_MESSAGE,5*1000L,TIMES_INFINITY,NULL);
#else
	m_pITimerEngine->SetTimer(IDI_LOAD_SYSTEM_MESSAGE,TIME_LOAD_SYSTEM_MESSAGE*1000L,TIMES_INFINITY,NULL);
	m_pITimerEngine->SetTimer(IDI_SEND_SYSTEM_MESSAGE,TIME_SEND_SYSTEM_MESSAGE*1000L,TIMES_INFINITY,NULL);
#endif

	//延时加载敏感词
	m_pITimerEngine->SetTimer(IDI_LOAD_SENSITIVE_WORD,TIME_LOAD_SENSITIVE_WORD*1000L,TIMES_INFINITY,NULL);

	m_pITimerEngine->SetTimer(IDI_DISTRIBUTE_USER,10*1000,TIMES_INFINITY,NULL);

	// 延时加载平衡分配置
	// m_pITimerEngine->SetTimer(IDI_LOAD_BALANCE_SCORE_CURVE,TIME_LOAD_BALANCE_SCORE_CURVE*1000L,TIMES_INFINITY,NULL);

	//广场赛开始计时
	m_pITimerEngine->SetTimer(IDI_SQUARE_MATCH_ONTIMER,1000L,TIMES_INFINITY,NULL);
	return true;
}

//停止事件
bool CAttemperEngineSink::OnAttemperEngineConclude(IUnknownEx * pIUnknownEx)
{
	//状态变量
	m_bCollectUser=false;
	m_bNeekCorrespond=true;

	//配置信息
	m_pInitParameter=NULL;
	m_pGameServiceAttrib=NULL;
	m_pGameServiceOption=NULL;
	m_pGameServiceTableOption = NULL;
// 	for(BYTE i=0; i<20; i++)memset(&m_GameSquareOptionArray[i],0,sizeof(m_GameSquareOptionArray[i]));

	//组件变量
	m_pITimerEngine=NULL;
	m_pITCPSocketService=NULL;
	m_pITCPNetworkEngine=NULL;

	//数据引擎
	m_pIRecordDataBaseEngine=NULL;
	m_pIKernelDataBaseEngine=NULL;

	//绑定数据
	SafeDeleteArray(m_pNormalParameter);
	SafeDeleteArray(m_pAndroidParameter);

	//删除桌子
	for (auto &iter : m_TableFrameArray)
	{
		SafeRelease(iter);
	}

	//删除用户
	m_TableFrameArray.clear();
	m_ServerUserManager.DeleteUserItem();
	m_ServerListManager.ResetServerList();
	m_WaitDistributeList.RemoveAll();
	
	//停止服务
	m_AndroidUserManager.ConcludeService();

	//停止比赛
	if(m_pIGameMatchServiceManager!=NULL)
		m_pIGameMatchServiceManager->StopService();

	//清除消息数据
	ClearSystemMessageData();

	//复位关键字
	m_WordsFilter.ResetSensitiveWordArray();

	// 复位平衡分曲线配置
	m_BalanceScoreCurve.ResetBalanceScoreCurve();

	// 清理玩家游戏房间锁
	if (m_pIDBCorrespondManager)
	{
		m_pIDBCorrespondManager->PostDataBaseRequest(0L, DBR_GR_RESET_GAME_SCORE_LOCKER, 0L, NULL, 0L);
	}

	//停止广场赛
	m_SquareManager.ConcludeService();

	return true;
}

//控制事件
bool CAttemperEngineSink::OnEventControl(WORD wIdentifier, VOID * pData, WORD wDataSize)
{
	switch (wIdentifier)
	{
	case CT_CONNECT_CORRESPOND:		//连接协调
		{
			//发起连接
			tagAddressInfo * pCorrespondAddress=&m_pInitParameter->m_CorrespondAddress;
			m_pITCPSocketService->Connect(pCorrespondAddress->szAddress,m_pInitParameter->m_wCorrespondPort);

			//构造提示
			TCHAR szString[512]=TEXT("");
			_sntprintf(szString,CountArray(szString),TEXT("正在连接协调服务器 [ %s:%d ]"),pCorrespondAddress->szAddress,m_pInitParameter->m_wCorrespondPort);

			//提示消息
			CTraceService::TraceString(szString,TraceLevel_Normal);
			LOGI("CAttemperEngineSink::OnEventControl, "<<szString);

			return true;
		}
	case CT_LOAD_SERVICE_CONFIG:	//加载配置
		{
			//加载配置
			m_pIDBCorrespondManager->PostDataBaseRequest(0L,DBR_GR_LOAD_PARAMETER,0L,NULL,0L);

			//加载列表
			m_pIDBCorrespondManager->PostDataBaseRequest(0L,DBR_GR_LOAD_GAME_COLUMN,0L,NULL,0L);

			//加载机器
			m_pIDBCorrespondManager->PostDataBaseRequest(0L,DBR_GR_LOAD_ANDROID_USER,0L,NULL,0L);

			//加载道具
			m_pIDBCorrespondManager->PostDataBaseRequest(0L,DBR_GR_LOAD_GAME_PROPERTY,0L,NULL,0L);

			//加载消息
			m_pIDBCorrespondManager->PostDataBaseRequest(0L,DBR_GR_LOAD_SYSTEM_MESSAGE,0L,NULL,0L);

			// 清理游戏房间锁
			m_pIDBCorrespondManager->PostDataBaseRequest(0L, DBR_GR_RESET_GAME_SCORE_LOCKER, 0L, NULL, 0L);

			return true;
		}
	}

	return false;
}

//调度事件
bool CAttemperEngineSink::OnEventAttemperData(WORD wRequestID, VOID * pData, WORD wDataSize)
{
	return false;
}

struct TableValue
{
	WORD wTableID;
	int dwScore;
};

bool sortbyscore(TableValue t1, TableValue t2)
{	
	if (t1.dwScore != t2.dwScore)
		return t1.dwScore > t2.dwScore;
	
	return (bool)(rand() % 2);
}

WORD CAttemperEngineSink::GetAndriodTable(IAndroidUserItem * pIAndroidUserItem)
{
//	LOGI("CAttemperEngineSink::GetAndriodTable");
	if (pIAndroidUserItem == NULL)
		return INVALID_TABLE;

	//每个桌子的评估分数
	vector<TableValue> valscores;

	bool bAllowDynamicJoin=CServerRule::IsAllowDynamicJoin(m_pGameServiceOption->dwServerRule);
	for (size_t i = 0; i < m_TableFrameArray.size(); i++)
	{
		TableValue tv;
		tv.wTableID = i;
		tv.dwScore = 0;

		//获取桌子
		CTableFrame * pTableFrame = m_TableFrameArray.at(i);
		if (pTableFrame->IsGameStarted()&&(!bAllowDynamicJoin))
		{
//			LOGI("CAttemperEngineSink::GetAndriodTable, IsGameStarted&&!bAllowDynamicJoin wTableID="<<tv.wTableID<<", dwScore="<<tv.dwScore);
			valscores.push_back(tv);
			continue;
		}

		//桌子状况
		tagTableUserInfo TableUserInfo;
		WORD wUserSitCount=pTableFrame->GetTableUserInfo(TableUserInfo);
		WORD wChairCount=pTableFrame->GetChairCount();

//		LOGI("CAttemperEngineSink::GetAndriodTable, wUserSitCount="<<wUserSitCount<<", wChairCount="<<wChairCount);
		if (wUserSitCount==wChairCount)
		{
//			LOGI("CAttemperEngineSink::GetAndriodTable, wTableID="<<tv.wTableID<<", dwScore="<<tv.dwScore);
			valscores.push_back(tv);
			continue;
		}

		WORD	wAndriodCount = 0;
		SCORE   sScore = 0;
		for (int j = 0; j < wChairCount; j++)
		{
			IServerUserItem* pUserItem = pTableFrame->GetTableUserItem(j);
			if (pUserItem==NULL)
				continue;
			sScore+=pUserItem->GetUserScore();

//			LOGI("CAttemperEngineSink::GetAndriodTable, j="<<j<<", sScore="<<sScore);
			if (pUserItem->IsAndroidUser())
				wAndriodCount++;
		}
//		LOGI("CAttemperEngineSink::GetAndriodTable, wAndriodCount="<<wAndriodCount);
		WORD ScoreVal = 0;  //分数的评估分
		if (wUserSitCount != 0)
		{
			sScore /= wUserSitCount;
//			LOGI("CAttemperEngineSink::GetAndriodTable, sScore="<<sScore);
			ScoreVal = (WORD)(log(abs(double(pIAndroidUserItem->GetMeUserItem()->GetUserScore() - sScore)))) * 10;

//			LOGI("CAttemperEngineSink::GetAndriodTable, ScoreVal="<<ScoreVal);
			if (ScoreVal > 5)
				ScoreVal = 5;
			if (ScoreVal <= 0)
				ScoreVal = 0;

			ScoreVal *= 5;
		}

        //座的越多越有机会被分配到
		//座位靠前越有机会被分配到
		//机器人越多越不会被分配到
		tv.dwScore = 100 + wUserSitCount * 50 + (m_pGameServiceOption->wTableCount / 2) - wAndriodCount * 20 - ScoreVal;
		if (tv.dwScore < 0)
		{
			ASSERT(0);
			tv.dwScore = 0;
		}
//		LOGI("CAttemperEngineSink::GetAndriodTable, wTableID="<<tv.wTableID<<", dwScore="<<tv.dwScore);
		valscores.push_back(tv);
	}

	int maxscore = -1;
	WORD maxidx = INVALID_TABLE;
	for (int i = 0; i < (int)valscores.size(); i++)
	{
		int score = 0;
		if (valscores[i].dwScore == 0)
			score = 0;
		else
		{
//			score = rand() % valscores[i].dwScore;

			// 此处不随机，采用分数大的就一定会安排的方案
			score = valscores[i].dwScore; 
		}

		if (score > maxscore)
		{
			maxscore = score;
			maxidx = i;
		}
	}

	//add by wangl 20150303
	//最大评估分数<=0，说明没有适合的桌子分配
	if(maxscore<=0)
	{
		maxidx=INVALID_TABLE;
	}

	return maxidx; 
};

int RandIndex(vector<int> all)
{
	if (all.size() == 0)
		return -1;

	if (all.size() == 1)
		return 0;

	int total = 0;
	vector<int> alltmp;
	alltmp.push_back(0); 
	for (size_t i = 0; i < all.size(); i++)
	{
		total += all[i];
		alltmp.push_back(total);
	}

	int rnd = rand() % total;
	for (size_t i = 0; i < alltmp.size() - 1; i++)
	{
		if (rnd >= alltmp[i] && rnd < alltmp[i+1])
		{
			return (int)i;
		}
	}

	return -1;
};

//时间事件
bool CAttemperEngineSink::OnEventTimer(DWORD dwTimerID, WPARAM wBindParam)
{
//	LOGI("CAttemperEngineSink::OnEventTimer, dwTimerID=" << dwTimerID);
// #ifdef _DEBUG
// 	HANDLE handle = GetCurrentProcess();
// 	PROCESS_MEMORY_COUNTERS pmc;
// 	GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 	LOGD("CAttemperEngineSink::OnEventTimer " << dwTimerID << " Before, WorkingSetSize = " << (pmc.WorkingSetSize));
// #endif
	//调度时间
	try
	{
		if ((dwTimerID>=IDI_MAIN_MODULE_START)&&(dwTimerID<=IDI_MAIN_MODULE_FINISH))
		{
			//时间处理
			switch (dwTimerID)
			{
			case IDI_LOAD_ANDROID_USER:		//加载机器
				{
					//加载机器
					m_pIDBCorrespondManager->PostDataBaseRequest(0L,DBR_GR_LOAD_ANDROID_USER,0L,NULL,0L);
// #ifdef _DEBUG
// 					GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 					LOGD("CAttemperEngineSink::OnEventTimer " << dwTimerID << " After, WorkingSetSize = " << (pmc.WorkingSetSize));
// #endif
					return true;
				}
			case IDI_ON_ANDROID_SEND_TRUMPT:
				{
					OnTCPAndroidSendTrumpet();
// #ifdef _DEBUG
// 					GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 					LOGD("CAttemperEngineSink::OnEventTimer " << dwTimerID << " After, WorkingSetSize = " << (pmc.WorkingSetSize));
// #endif
					return true;
				}
			case IDI_REPORT_SERVER_INFO:	//房间信息
				{
					//变量定义
					CMD_CS_C_ServerOnLine ServerOnLine;
					ZeroMemory(&ServerOnLine,sizeof(ServerOnLine));

					//设置变量
					ServerOnLine.dwOnLineCount=m_ServerUserManager.GetUserItemCount();

					//发送数据
					m_pITCPSocketService->SendData(MDM_CS_SERVICE_INFO,SUB_CS_C_SERVER_ONLINE,&ServerOnLine,sizeof(ServerOnLine));
// #ifdef _DEBUG
// 					GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 					LOGD("CAttemperEngineSink::OnEventTimer " << dwTimerID << " After, WorkingSetSize = " << (pmc.WorkingSetSize));
// #endif
					return true;
				}
			case IDI_CONNECT_CORRESPOND:	//连接协调
				{
					//发起连接
					tagAddressInfo * pCorrespondAddress=&m_pInitParameter->m_CorrespondAddress;
					m_pITCPSocketService->Connect(pCorrespondAddress->szAddress,m_pInitParameter->m_wCorrespondPort);

					//构造提示
					TCHAR szString[512]=TEXT("");
					_sntprintf(szString,CountArray(szString),TEXT("正在连接协调服务器 [ %s:%d ]"),pCorrespondAddress->szAddress,m_pInitParameter->m_wCorrespondPort);

					//提示消息
					CTraceService::TraceString(szString,TraceLevel_Normal);
					LOGI("CAttemperEngineSink::OnEventTimer, "<<szString);
// #ifdef _DEBUG
// 					GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 					LOGD("CAttemperEngineSink::OnEventTimer " << dwTimerID << " After, WorkingSetSize = " << (pmc.WorkingSetSize));
// #endif
					return true;
				}
			case IDI_GAME_SERVICE_PULSE:	//服务维护
				{
					return true;
				}
			case IDI_DISTRIBUTE_ANDROID:	//分配机器
				{
					//动作处理
					if (m_AndroidUserManager.GetAndroidCount()>0 && m_pGameServiceOption->wServerType!=GAME_GENRE_MATCH)
					{
						//变量定义
						bool bAllowDynamicJoin=CServerRule::IsAllowDynamicJoin(m_pGameServiceOption->dwServerRule);
						bool bAllowAndroidAttend=CServerRule::IsAllowAndroidAttend(m_pGameServiceOption->dwServerRule);
						bool bAllowAndroidSimulate=CServerRule::IsAllowAndroidSimulate(m_pGameServiceOption->dwServerRule);
						bool bAllowAvertCheatMode=(CServerRule::IsAllowAvertCheatMode(m_pGameServiceOption->dwServerRule)&&(m_pGameServiceAttrib->wChairCount < MAX_CHAIR));

						//模拟处理
						//允许机器人占位
						if (bAllowAndroidSimulate==true && bAllowAvertCheatMode==false)
						{
							//机器状态
							tagAndroidUserInfo AndroidSimulate; 
							m_AndroidUserManager.GetAndroidUserInfo(AndroidSimulate,ANDROID_SIMULATE);

							//机器处理
							if (AndroidSimulate.wFreeUserCount>0)
							{
 								for (WORD ii=0;ii<1;ii++)   // 这句代码不明确意思，删除掉
 								{
									//随机桌子，在前1/4的座位选择
									WORD wTableID=rand()%(__max(m_pGameServiceOption->wTableCount/3,1));

									//获取桌子
									CTableFrame * pTableFrame=m_TableFrameArray[wTableID];
									if ((pTableFrame->IsGameStarted()==true)&&(bAllowDynamicJoin==false)) continue;

									//桌子状况
									tagTableUserInfo TableUserInfo;
									WORD wUserSitCount=pTableFrame->GetTableUserInfo(TableUserInfo);

									//分配判断
									if (TableUserInfo.wTableUserCount>0) continue;
									if ((wUserSitCount>=TableUserInfo.wMinUserCount)&&(m_pGameServiceAttrib->wChairCount<MAX_CHAIR)) continue;

									vector<int> scores; //座位评估分
									for (int i = 0; i <= m_pGameServiceAttrib->wChairCount - TableUserInfo.wMinUserCount; i++)
									{
										int want = m_pGameServiceAttrib->wChairCount - i;
										scores.push_back(want * want);		
									}

									WORD wWantAndroidCount=m_pGameServiceAttrib->wChairCount-RandIndex(scores);
									//坐下判断
									if (AndroidSimulate.wFreeUserCount>=wWantAndroidCount)
									{
										//变量定义
										WORD wHandleCount=0;

										//坐下处理
										for (WORD j=0;j<AndroidSimulate.wFreeUserCount;j++)
										{
											//变量定义
											WORD wChairID=pTableFrame->GetRandNullChairID();

											//无效过滤
											//ASSERT(wChairID!=INVALID_CHAIR);
											if (wChairID==INVALID_CHAIR) continue;

											//用户坐下
											IAndroidUserItem * pIAndroidUserItem=AndroidSimulate.pIAndroidUserFree[j];
											if (pTableFrame->PerformSitDownAction(wChairID,pIAndroidUserItem->GetMeUserItem())==true)
											{
												//设置变量
												wHandleCount++;

												//完成判断
												if (wHandleCount>=wWantAndroidCount) 
												{
													return true;
												}
											}
										}

										if(wHandleCount > 0) return true;
									}
								}
							}
						}

						//陪打处理（被动陪打）
						if (bAllowAndroidAttend==true)
						{
							LOGI("CAttemperEngineSink::OnEventTimer, IDI_DISTRIBUTE_ANDROID, ANDROID_PASSIVITY");
							//被动状态
							tagAndroidUserInfo AndroidPassivity;
							m_AndroidUserManager.GetAndroidUserInfo(AndroidPassivity,ANDROID_PASSIVITY);

							if(bAllowAvertCheatMode)
							{
								//坐下处理
								for (WORD j=0;j<AndroidPassivity.wFreeUserCount;j++)
								{
									IAndroidUserItem * pIAndroidUserItem=AndroidPassivity.pIAndroidUserFree[j];
									if (InsertWaitDistribute(pIAndroidUserItem->GetMeUserItem())==true) return true;
								}
							}
							else
							{
								//被动处理
								if (AndroidPassivity.wFreeUserCount>0)
								{
									//百人游戏
									if(m_pGameServiceAttrib->wChairCount >= MAX_CHAIR)
									{
										for (INT_PTR i=0;i<(m_pGameServiceOption->wTableCount);i++)
										{
											//获取桌子
											CTableFrame * pTableFrame=m_TableFrameArray[i];
											if ((pTableFrame->IsGameStarted()==true)&&(bAllowDynamicJoin==false))
											{
												LOGI("CAttemperEngineSink::OnEventTimer, pTableFrame->IsGameStarted()==true&&bAllowDynamicJoin==false");
												continue;
											}

											//桌子状况
											tagTableUserInfo TableUserInfo;
											WORD wUserSitCount=pTableFrame->GetTableUserInfo(TableUserInfo);

											//分配判断
											if (wUserSitCount>m_pGameServiceAttrib->wChairCount*2/3)
											{
												LOGI("CAttemperEngineSink::OnEventTimer, wFreeUserCount="<<AndroidPassivity.wFreeUserCount<<", wChairCount="<<m_pGameServiceAttrib->wChairCount);
												continue;
											}

											//变量定义
											IServerUserItem * pIServerUserItem=NULL;
											WORD wChairID=pTableFrame->GetRandNullChairID();

											//无效过滤
											ASSERT(wChairID!=INVALID_CHAIR);
											if (wChairID==INVALID_CHAIR)
											{
												LOGI("CAttemperEngineSink::OnEventTimer, wChairID==INVALID_CHAIR");
												continue;
											}

											//坐下处理
											for (WORD j=0;j<AndroidPassivity.wFreeUserCount;j++)
											{
												IAndroidUserItem * pIAndroidUserItem=AndroidPassivity.pIAndroidUserFree[j];
												if (pTableFrame->PerformSitDownAction(wChairID,pIAndroidUserItem->GetMeUserItem())==true)
												{
													// 不return 继续处理
													return true;
												}
											}
										}
									}
									else
									{
										//坐下处理
										for (WORD j=0;j<AndroidPassivity.wFreeUserCount;j++)
										{
											IAndroidUserItem * pIAndroidUserItem=AndroidPassivity.pIAndroidUserFree[j];
											if (pIAndroidUserItem == NULL)
												continue;

											WORD wTableID = GetAndriodTable(pIAndroidUserItem);
											if (wTableID == INVALID_TABLE)
											{
												LOGI("CAttemperEngineSink::OnEventTimer, wTableID="<<wTableID);
												continue;
											}

											CTableFrame * pTableFrame=m_TableFrameArray[wTableID];
											if ((pTableFrame->IsGameStarted()==true)&&(bAllowDynamicJoin==false))
											{
												LOGI("CAttemperEngineSink::OnEventTimer, pTableFrame->IsGameStarted()==true&&bAllowDynamicJoin==false");
												continue;
											}

											//桌子状况
											tagTableUserInfo TableUserInfo;
											WORD wUserSitCount=pTableFrame->GetTableUserInfo(TableUserInfo);

											//分配判断
											if (wUserSitCount==0) 
											{
												LOGI("CAttemperEngineSink::OnEventTimer, wUserSitCount==0");
												continue;
											}
											if (TableUserInfo.wTableUserCount==0) continue;
											if ((wUserSitCount>=TableUserInfo.wMinUserCount))
											{
												LOGI("CAttemperEngineSink::OnEventTimer, wUserSitCount=="<<wUserSitCount<<", TableUserInfo.wMinUserCount="<<TableUserInfo.wMinUserCount);
												continue;
											}

											WORD wChairID=pTableFrame->GetRandNullChairID();
											if (wChairID==INVALID_CHAIR)
											{
												LOGI("CAttemperEngineSink::OnEventTimer, wChairID=INVALID_CHAIR");
												continue;
											}

											if (pTableFrame->PerformSitDownAction(wChairID,pIAndroidUserItem->GetMeUserItem())==true)
											{
												// 不返回，继续处理
												return true;
//												break;
											}
										}
									}
								}
							}
						}

						//陪打处理（主动陪打）
						if (bAllowAndroidAttend==true)
						{
							LOGI("CAttemperEngineSink::OnEventTimer, IDI_DISTRIBUTE_ANDROID, ANDROID_INITIATIVE");
							//主动状态
							tagAndroidUserInfo AndroidInitiative;
							m_AndroidUserManager.GetAndroidUserInfo(AndroidInitiative,ANDROID_INITIATIVE);
							WORD wAllAndroidCount = AndroidInitiative.wFreeUserCount+AndroidInitiative.wPlayUserCount+AndroidInitiative.wSitdownUserCount;

							if(bAllowAvertCheatMode)
							{
								//坐下处理(防作弊模式)
								for (WORD j=0;j<AndroidInitiative.wFreeUserCount;j++)
								{
									IAndroidUserItem * pIAndroidUserItem=AndroidInitiative.pIAndroidUserFree[j];
									if (InsertWaitDistribute(pIAndroidUserItem->GetMeUserItem())==true) return true;
								}
							}
							else
							{
//								LOGI("CAttemperEngineSink::OnEventTimer, wFreeUserCount="<<AndroidInitiative.wFreeUserCount);
								//坐下处理
								for (WORD j=0;j<AndroidInitiative.wFreeUserCount;j++)
								{
									IAndroidUserItem * pIAndroidUserItem=AndroidInitiative.pIAndroidUserFree[j];
									if (pIAndroidUserItem == NULL)
									{
										LOGI("CAttemperEngineSink::OnEventTimer, pIAndroidUserItem==NULL");
										continue;
									}

									// 根据当前玩家的入座概率,判断是否可以入座
									tagAndroidService* AndroidService=pIAndroidUserItem->GetAndroidService();
									tagAndroidParameter* AndroidParameter=pIAndroidUserItem->GetAndroidParameter();
									if (AndroidService == NULL || AndroidParameter==NULL)
									{
										LOGI("CAttemperEngineSink::OnEventTimer, AndroidService==NULL || AndroidParameter == NULL");
										continue;
									}
									if (AndroidService->dwSitRate > AndroidParameter->dwSitRate)
									{
										LOGI("CAttemperEngineSink::OnEventTimer, AndroidService->dwSitRate="<<AndroidService->dwSitRate<<", AndroidParameter->dwSitRate="<<AndroidParameter->dwSitRate);
										continue;
									}

									WORD wTableID = GetAndriodTable(pIAndroidUserItem);
									if (wTableID == INVALID_TABLE)
									{
										LOGI("CAttemperEngineSink::OnEventTimer, wTableID="<<wTableID);
										continue;
									}

									CTableFrame * pTableFrame=m_TableFrameArray[wTableID];
									if ((pTableFrame->IsGameStarted()==true)&&(bAllowDynamicJoin==false))
									{
										LOGI("CAttemperEngineSink::OnEventTimer, pTableFrame->IsGameStarted()==true&&bAllowDynamicJoin==false");
										continue;
									}

									WORD wChairID=pTableFrame->GetRandNullChairID();

									//无效过滤
									ASSERT(wChairID!=INVALID_CHAIR);
									if (wChairID==INVALID_CHAIR)
									{
										LOGI("CAttemperEngineSink::OnEventTimer, wChairID==INVALID_CHAIR");
										continue;
									}

									if (pTableFrame->PerformSitDownAction(wChairID,pIAndroidUserItem->GetMeUserItem())==true)
									{
										// 不返回，继续处理
										return true;
//										break;
									}
								}
							}
						}

						//起立处理(暂时屏蔽)
// 						WORD wStandUpCount=0;
// 						WORD wRandCount=((rand()%3)+1);
// 						INT_PTR nIndex = rand()%(__max(m_pGameServiceOption->wTableCount,1));
// 						for (INT_PTR i=nIndex;i<m_pGameServiceOption->wTableCount+nIndex;++i)
// 						{
// 							//获取桌子
// 							INT_PTR nTableIndex=i%m_pGameServiceOption->wTableCount;
// 							CTableFrame * pTableFrame=m_TableFrameArray[nTableIndex];
// 							if (pTableFrame->IsGameStarted()==true) continue;
// 
// 							//桌子状况
// 							tagTableUserInfo TableUserInfo;
// 							WORD wUserSitCount=pTableFrame->GetTableUserInfo(TableUserInfo);
// 
// 							//用户过虑
// 							bool bRand = ((rand()%100)>50);
// 							if (TableUserInfo.wTableAndroidCount==0) continue;
// 							if ((TableUserInfo.wTableUserCount>0)&&(bAllowAndroidAttend==true) && bRand) continue;
// 							if (TableUserInfo.wTableAndroidCount>=TableUserInfo.wMinUserCount && bRand) continue;
// 
// 							//起立处理
// 							for (WORD j=0;j<pTableFrame->GetChairCount();j++)
// 							{
// 								//获取用户
// 								IServerUserItem * pIServerUserItem=pTableFrame->GetTableUserItem(j);
// 								if (pIServerUserItem==NULL) continue;
// 
// 								//用户起立
// 								if ((pIServerUserItem->IsAndroidUser()==true))
// 								{
// 									if ( (pIServerUserItem->GetUserStatus()==US_SIT) || (pIServerUserItem->GetUserStatus()==US_READY) || (pIServerUserItem->GetUserStatus()==US_LOOKON))
// 									{
// 										LOGI("CAttemperEngineSink::OnEventTimer PerformStandUpAction, NickName="<<pIServerUserItem->GetNickName());
// 										if (pTableFrame->PerformStandUpAction(pIServerUserItem)==true)
// 										{
// 											wStandUpCount++;
// 											if(wStandUpCount>=wRandCount)
// 												return true;
// 											else
// 												break;
// 										}
// 									}
// 								}
// 							}
// 						}
					}

					return true;
				}
			case IDI_DBCORRESPOND_NOTIFY: //缓存定时处理
				{
					if (m_pIDBCorrespondManager)
					{
						m_pIDBCorrespondManager->OnTimerNotify();
					}
					return true;
				}
			case IDI_LOAD_SYSTEM_MESSAGE: //系统消息
				{
					//清除消息数据
					ClearSystemMessageData();

					//加载消息
					m_pIDBCorrespondManager->PostDataBaseRequest(0L,DBR_GR_LOAD_SYSTEM_MESSAGE,0L,NULL,0L);
					return true;
				}
			case IDI_LOAD_SENSITIVE_WORD:	//加载敏感词
				{
					//投递请求
					m_pIRecordDataBaseEngine->PostDataBaseRequest(DBR_GR_LOAD_SENSITIVE_WORDS,0,NULL,0);	
					return true;
				}
			case IDI_SEND_SYSTEM_MESSAGE: //系统消息
				{
					//数量判断
					if(m_SystemMessageList.GetCount()==0) return true;

					//时效判断
					DWORD dwCurrTime = (DWORD)time(NULL);
					POSITION pos = m_SystemMessageList.GetHeadPosition();
					while(pos != NULL)
					{
						//获取消息
						POSITION tempPos = pos;
						tagSystemMessage *pTagSystemMessage = m_SystemMessageList.GetNext(pos);

						//时效判断
						if(pTagSystemMessage->SystemMessage.tConcludeTime < dwCurrTime)
						{
							m_SystemMessageList.RemoveAt(tempPos);
							delete pTagSystemMessage;
							continue;
						}

						//间隔判断
						if(pTagSystemMessage->dwLastTime+pTagSystemMessage->SystemMessage.dwTimeRate < dwCurrTime)
						{
							//更新数据
							pTagSystemMessage->dwLastTime=dwCurrTime;

							//构造消息
							CMD_GR_SendMessage SendMessage = {};
							SendMessage.cbAllRoom = (pTagSystemMessage->SystemMessage.cbMessageID==100)?TRUE:FALSE;
							SendMessage.cbGame = (pTagSystemMessage->SystemMessage.cbMessageType==1)?TRUE:FALSE;
							SendMessage.cbRoom = (pTagSystemMessage->SystemMessage.cbMessageType==2)?TRUE:FALSE;
							if(pTagSystemMessage->SystemMessage.cbMessageType==3)
							{
								SendMessage.cbGame = TRUE;
								SendMessage.cbRoom = TRUE;
							}
							lstrcpyn(SendMessage.szSystemMessage,pTagSystemMessage->SystemMessage.szSystemMessage,CountArray(SendMessage.szSystemMessage));
							SendMessage.wChatLength = lstrlen(SendMessage.szSystemMessage)+1;

							//发送消息
							WORD wSendSize = sizeof(SendMessage)-sizeof(SendMessage.szSystemMessage)+CountStringBuffer(SendMessage.szSystemMessage);
							SendSystemMessage(&SendMessage,wSendSize);
						}
					}
					return true;
				}
			case IDI_DISTRIBUTE_USER: //分配用户
				{
					DistributeUserGame();
					return true;
				}
// 			case IDI_LOAD_BALANCE_SCORE_CURVE:	//平衡分曲线
// 				{
// 					//投递请求
// 					m_pIRecordDataBaseEngine->PostDataBaseRequest(DBR_GR_LOAD_BALANCE_SCORE_CURVE, 0, NULL, 0);		
// 					return true;
// 				}
			case IDI_SQUARE_MATCH_ONTIMER:		//广场赛计时器
				{
					m_SquareManager.OnTimer();
					return true;
				}
			}
		}
	}
	catch(...)
	{
		LOGI("CAttemperEngineSink::OnEventTimer catch, dwTimerID="<<dwTimerID);
	}

	try
	{
		//机器时器
		if ((dwTimerID>=IDI_REBOT_MODULE_START)&&(dwTimerID<=IDI_REBOT_MODULE_FINISH))
		{
			//时间处理
			m_AndroidUserManager.OnEventTimerPulse(dwTimerID,wBindParam);
			return true;
		}
	}
	catch(...)
	{
		LOGI("CAttemperEngineSink::OnEventTimer catch, dwTimerID="<<dwTimerID);
	}

	try
	{
		//比赛定时器
		if((dwTimerID>=IDI_MATCH_MODULE_START)&&(dwTimerID<IDI_MATCH_MODULE_FINISH))
		{
			if (m_pIGameMatchServiceManager != NULL)
			{
				m_pIGameMatchServiceManager->OnEventTimer(dwTimerID, wBindParam);
			}
			return true;
		}
	}
	catch(...)
	{
		LOGI("CAttemperEngineSink::OnEventTimer catch, dwTimerID="<<dwTimerID);
	}

	
	try
	{
		//桌子时间(需要判断当前的模式,不同的模式,传递给不同的定时器调用)
		if ((dwTimerID>=IDI_TABLE_MODULE_START)&&(dwTimerID<=IDI_TABLE_MODULE_FINISH))
		{
			//桌子号码
			DWORD dwTableTimerID=dwTimerID-IDI_TABLE_MODULE_START;
			WORD wTableID=(WORD)(dwTableTimerID/TIME_TABLE_MODULE_RANGE);

			//时间效验
			if (wTableID>=(WORD)m_TableFrameArray.size()) 
			{
				ASSERT(FALSE);
				return false;
			}

			//时间通知
			CTableFrame * pTableFrame=m_TableFrameArray[wTableID];
			bool flag = pTableFrame->OnEventTimer(dwTableTimerID%TIME_TABLE_MODULE_RANGE,wBindParam);
			return flag;
		}
	}
	catch(...)
	{
		LOGI("CAttemperEngineSink::OnEventTimer catch, dwTimerID="<<dwTimerID);
	}

	return false;
}

//数据库事件
bool CAttemperEngineSink::OnEventDataBase(WORD wRequestID, DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	switch (wRequestID)
	{
	case DBO_GR_LOGON_SUCCESS:			//登录成功
		{
			return OnDBLogonSuccess(dwContextID,pData,wDataSize);
		}
	case DBO_GR_LOGON_FAILURE:			//登录失败
		{
			return OnDBLogonFailure(dwContextID,pData,wDataSize);
		}
	case DBO_GR_GAME_PARAMETER:			//游戏参数
		{
			return OnDBGameParameter(dwContextID,pData,wDataSize);
		}
	case DBO_GR_GAME_COLUMN_INFO:		//列表信息
		{
			return OnDBGameColumnInfo(dwContextID,pData,wDataSize);
		}
	case DBR_GR_GAME_ANDROID_INFO:		//机器信息
		{
			return OnDBGameAndroidInfo(dwContextID,pData,wDataSize);
		}
	case DBO_GR_GAME_PROPERTY_INFO:		//道具信息
		{
			return OnDBGamePropertyInfo(dwContextID,pData,wDataSize);
		}
	case DBO_GR_USER_INSURE_INFO:		//银行信息
		{
			return OnDBUserInsureInfo(dwContextID,pData,wDataSize);
		}
    case DBR_GR_TRANS_RECORD:
        {
            return OnDBUserTransRecord(dwContextID,pData,wDataSize);
        }
	case DBO_GR_USER_INSURE_SUCCESS:	//银行成功
		{
			return OnDBUserInsureSuccess(dwContextID,pData,wDataSize);
		}
	case DBO_GR_USER_INSURE_FAILURE:	//银行失败
		{
			return OnDBUserInsureFailure(dwContextID,pData,wDataSize);
		}
	case DBO_GR_USER_INSURE_USER_INFO:  //用户信息
		{
			return OnDBUserInsureUserInfo(dwContextID,pData,wDataSize);
		}
	case DBO_GR_PROPERTY_SUCCESS:		//道具成功
		{
			return OnDBPropertySuccess(dwContextID,pData,wDataSize);
		}
	case DBO_GR_PROPERTY_FAILURE:		//道具失败
		{
			return OnDBPropertyFailure(dwContextID,pData,wDataSize);
		}
	case DBO_GR_SYSTEM_MESSAGE_RESULT:  //系统消息
		{
			return OnDBSystemMessage(dwContextID,pData,wDataSize);
		}
	case DBO_GR_SENSITIVE_WORDS:		//加载敏感词
		{
			return OnDBSensitiveWords(dwContextID,pData,wDataSize);
		}
	case DBO_GR_INSURE_TRANS_RECEIPT:	//转账回执
		{
			return OnDBUserTransferReceipt(dwContextID, pData, wDataSize);
		}
	case DBO_GR_OPERATE_SUCCESS:		//操作成功
		{
			return OnDBUserOperateSuccess(dwContextID, pData, wDataSize);
		}
	case DBO_GR_OPERATE_FAILURE:		//操作失败
		{	
			return OnDBUserOperateFailure(dwContextID, pData, wDataSize);
		}
	case DBO_GR_BALANCE_SCORE_CURVE:	//平衡分配置
		{
			return OnDBBalanceScoreCurve(dwContextID, pData, wDataSize);
		}
	case DBO_GR_USER_BALANCE_SCORE:		//玩家平衡分数据
		{
			return OnDBUserBalanceScore(dwContextID, pData, wDataSize);
		}
	case DBO_GR_SQUARE_REWARD:			//广场赛奖励信息
		{
			return OnDBSquareReward(dwContextID, pData, wDataSize);
		}
	case DBO_GR_LOAD_SQUARE_REWARD:			//加载广场赛奖励配置信息
		{
			return OnDBLoadSquareReward(dwContextID, pData, wDataSize);
		}
	case DBO_GR_CREATE_CUSTOMTABLE_SUCCESS:	// 创建桌子条件符合
		{
			return OnDBCreateCustomTableSuccess(dwContextID, pData, wDataSize);
		}
	case DBO_GR_CREATE_CUSTOMTABLE_FAILURE:	// 创建桌子失败
		{
			return OnDBCreateCustomTableFailed(dwContextID, pData, wDataSize);
		}
	case DBO_GR_DISMISS_CUSTOMTABLE_SUCCESS:	// 解散桌子成功
		{
			return OnDBDismissCustomTableSuccess(dwContextID, pData, wDataSize);
		}
	case DBO_GR_DISMISS_CUSTOMTABLE_FAILURE:	// 解散桌子失败
		{
			return OnDBDismissCustomTableFailed(dwContextID, pData, wDataSize);
		}
	case DBO_GR_JOIN_CUSTOMTABLE_SUCCESS:		// 加入桌子成功
		{
			return OnDBJoinCustomTableSuccess(dwContextID, pData, wDataSize);
		}
	case DBO_GR_JOIN_CUSTOMTABLE_FAILURE:		// 加入桌子成功
		{	
			return OnDBJoinCustomTableFailed(dwContextID, pData, wDataSize);
		}
	case DBO_GR_USER_ROUNDSCORE_IN_CUSTOMTABLE:	// 玩家分数
		{
			return OnDBUserRoundScoreInCustomTable(dwContextID, pData, wDataSize);
		}
	}

	//比赛事件
	if(wRequestID>=DBO_GR_MATCH_EVENT_START && wRequestID<=DBO_GR_MATCH_EVENT_END)
	{
		//参数效验
		if(m_pIGameMatchServiceManager==NULL) return false;

		tagBindParameter * pBindParameter=GetBindParameter(LOWORD(dwContextID));
		IServerUserItem *pIServerUserItem=pBindParameter!=NULL?pBindParameter->pIServerUserItem:NULL;

		//废弃判断
		if ((pBindParameter->pIServerUserItem==NULL)||(pBindParameter->dwSocketID!=dwContextID))
		{
			//错误断言
			ASSERT(FALSE);
			return true;
		}

		return m_pIGameMatchServiceManager->OnEventDataBase(wRequestID,pIServerUserItem,pData,wDataSize);
	}

	// 用户自建桌子事件


	return false;
}

//关闭事件
bool CAttemperEngineSink::OnEventTCPSocketShut(WORD wServiceID, BYTE cbShutReason)
{
	//协调连接
	if (wServiceID==NETWORK_CORRESPOND)
	{
		//设置变量
		m_bCollectUser=false;

		//删除时间
		m_pITimerEngine->KillTimer(IDI_REPORT_SERVER_INFO);

		//重连判断
		if (m_bNeekCorrespond==true)
		{
			//构造提示
			TCHAR szDescribe[128]=TEXT("");
			_sntprintf(szDescribe,CountArray(szDescribe),TEXT("与协调服务器的连接关闭了，%ld 秒后将重新连接"),m_pInitParameter->m_wConnectTime);

			//提示消息
			CTraceService::TraceString(szDescribe,TraceLevel_Warning);
			LOGI(TEXT("CAttemperEngineSink::OnEventTCPSocketShut, ")<<szDescribe);

			//设置时间
			ASSERT(m_pITimerEngine!=NULL);
			m_pITimerEngine->SetTimer(IDI_CONNECT_CORRESPOND,m_pInitParameter->m_wConnectTime*1000L,1,0);
		}

		return true;
	}

	return false;
}

//连接事件
bool CAttemperEngineSink::OnEventTCPSocketLink(WORD wServiceID, INT nErrorCode)
{
	//协调连接
	if (wServiceID==NETWORK_CORRESPOND)
	{
		//错误判断
		if (nErrorCode!=0)
		{
			//构造提示
			TCHAR szDescribe[128]=TEXT("");
			_sntprintf(szDescribe,CountArray(szDescribe),TEXT("协调服务器连接失败 [ %ld ]，%ld 秒后将重新连接"),
				nErrorCode,m_pInitParameter->m_wConnectTime);

			//提示消息
			CTraceService::TraceString(szDescribe,TraceLevel_Warning);
			LOGI("CAttemperEngineSink::OnEventTCPSocketLink, "<<szDescribe);

			//设置时间
			ASSERT(m_pITimerEngine!=NULL);
			m_pITimerEngine->SetTimer(IDI_CONNECT_CORRESPOND,m_pInitParameter->m_wConnectTime*1000L,1,0);

			return false;
		}

		//提示消息
		CTraceService::TraceString(TEXT("正在发送游戏房间注册信息..."),TraceLevel_Normal);
		LOGI("CAttemperEngineSink::OnEventTCPSocketLink, "<<TEXT("正在发送游戏房间注册信息..."));

		//变量定义
		CMD_CS_C_RegisterServer RegisterServer;
		ZeroMemory(&RegisterServer,sizeof(RegisterServer));

		//服务端口
		CServiceUnits * pServiceUnits=CServiceUnits::g_pServiceUnits;
		//构造数据
		memcpy(&(RegisterServer.GameServerOption), m_pGameServiceOption, sizeof(RegisterServer.GameServerOption));
		RegisterServer.GameServerOption.wServerPort=pServiceUnits->m_TCPNetworkEngine->GetCurrentPort();
//		RegisterServer.wKindID=m_pGameServiceOption->wKindID;
// 		RegisterServer.wNodeID=m_pGameServiceOption->wNodeID;
// 		RegisterServer.wSortID=m_pGameServiceOption->wSortID;
// 		RegisterServer.wServerID=m_pGameServiceOption->wServerID;
		RegisterServer.dwOnLineCount=m_ServerUserManager.GetUserItemCount();
 		RegisterServer.dwFullCount=m_pGameServiceOption->wMaxPlayer-RESERVE_USER_COUNT;
// 		lstrcpyn(RegisterServer.szServerName,m_pGameServiceOption->szServerName,CountArray(RegisterServer.szServerName));
		lstrcpyn(RegisterServer.szServerAddr,m_pInitParameter->m_ServiceAddress.szAddress,CountArray(RegisterServer.szServerAddr));
		
		//发送数据
		ASSERT(m_pITCPSocketService!=NULL);
		m_pITCPSocketService->SendData(MDM_CS_REGISTER, SUB_CS_C_REGISTER_SERVER, &RegisterServer, sizeof(CMD_CS_C_RegisterServer));

		//设置时间
		ASSERT(m_pITimerEngine!=NULL);
		m_pITimerEngine->SetTimer(IDI_REPORT_SERVER_INFO,TIME_REPORT_SERVER_INFO*1000L,TIMES_INFINITY,0);

		return true;
	}

	return true;
}

//读取事件
bool CAttemperEngineSink::OnEventTCPSocketRead(WORD wServiceID, TCP_Command Command, VOID * pData, WORD wDataSize)
{
	LOGD("CAttemperEngineSink::OnEventTCPSocketRead, Command.wMainCmdID=" << Command.wMainCmdID << ", Command.wSubCmdID=" << Command.wSubCmdID);
	//协调连接
	if (wServiceID==NETWORK_CORRESPOND)
	{
		switch (Command.wMainCmdID)
		{
		case MDM_CS_REGISTER:		//注册服务
			{
				bool flag = OnTCPSocketMainRegister(Command.wSubCmdID,pData,wDataSize);
				return flag;
			}
		case MDM_CS_SERVICE_INFO:	//服务信息
			{
				bool flag = OnTCPSocketMainServiceInfo(Command.wSubCmdID, pData, wDataSize);
				return flag;
			}
		case MDM_CS_USER_COLLECT:	//用户汇总
			{
				bool flag = OnTCPSocketMainUserCollect(Command.wSubCmdID, pData, wDataSize);
				return flag;
			}
		case MDM_CS_REMOTE_SERVICE:	//远程服务
			{
				bool flag = OnTCPSocketMainRemoteService(Command.wSubCmdID, pData, wDataSize);
				return flag;
			}
		case MDM_CS_MANAGER_SERVICE: //管理服务
			{
				bool flag = OnTCPSocketMainManagerService(Command.wSubCmdID, pData, wDataSize);
				return flag;
			}
		}
	}

	//错误断言
	ASSERT(FALSE);

	return true;
}

//应答事件
bool CAttemperEngineSink::OnEventTCPNetworkBind(DWORD dwClientAddr, DWORD dwSocketID)
{
	LOGI("CAttemperEngineSink::OnEventTCPNetworkBind, dwClientAddr="<<dwClientAddr<<", dwSocketID="<<dwSocketID);
#ifdef _DEBUG
	HANDLE handle = GetCurrentProcess();
	PROCESS_MEMORY_COUNTERS pmc;
	GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
	LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
#endif

	//变量定义
	WORD wBindIndex=LOWORD(dwSocketID);
	tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);

	//设置变量
	if (pBindParameter!=NULL)
	{
		pBindParameter->dwSocketID=dwSocketID;
		pBindParameter->dwClientAddr=dwClientAddr;
		pBindParameter->dwActiveTime=(DWORD)time(NULL);

// #ifdef _DEBUG
// 		HANDLE handle = GetCurrentProcess();
// 		PROCESS_MEMORY_COUNTERS pmc;
// 		GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 		LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
// #endif
		return true;
	}

	//错误断言
	ASSERT(FALSE);

	return false;
}

//关闭事件
bool CAttemperEngineSink::OnEventTCPNetworkShut(DWORD dwClientAddr, DWORD dwActiveTime, DWORD dwSocketID)
{
	LOGI("CAttemperEngineSink::OnEventTCPNetworkShut, dwClientAddr="<<dwClientAddr<<", dwSocketID="<<dwSocketID<<", dwActiveTime="<<dwActiveTime);
// #ifdef _DEBUG
// 	HANDLE handle = GetCurrentProcess();
// 	PROCESS_MEMORY_COUNTERS pmc;
// 	GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 	LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
// #endif
	try
	{
		//变量定义
		WORD wBindIndex=LOWORD(dwSocketID);
		tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);

		//获取用户
		IServerUserItem * pIServerUserItem=pBindParameter->pIServerUserItem;

		//用户处理
		if (pIServerUserItem!=NULL)
		{
			LOGI("CAttemperEngineSink::OnEventTCPNetworkShut, UserID=" << pIServerUserItem->GetUserID() << ", szNickName=" << pIServerUserItem->GetNickName() << ", UserStatus=" << pIServerUserItem->GetUserStatus());
			if (pIServerUserItem->GetUserStatus() == US_TRUSTEE)
			{
				// 如果已经是托退状态，这时候断线，不应该再次退出
				pIServerUserItem->DetachBindStatus();
				ZeroMemory(pBindParameter,sizeof(tagBindParameter));
			}
			else
			{
				//变量定义
				WORD wTableID=pIServerUserItem->GetTableID();

				//断线处理
				if (wTableID!=INVALID_TABLE)
				{
					//解除绑定
					pIServerUserItem->DetachBindStatus();

					//断线通知
					ASSERT(wTableID<m_pGameServiceOption->wTableCount);
					m_TableFrameArray[wTableID]->OnEventUserOffLine(pIServerUserItem);
				}
				else
				{
					pIServerUserItem->SetUserStatus(US_NULL,INVALID_TABLE,INVALID_CHAIR);
				}
			}		
		}

		//清除信息
		ZeroMemory(pBindParameter,sizeof(tagBindParameter));
	}
	catch(...)
	{
		LOGE("CAttemperEngineSink::OnEventTCPNetworkShut, catch");
	}
// #ifdef _DEBUG
// // 	HANDLE handle = GetCurrentProcess();
// // 	PROCESS_MEMORY_COUNTERS pmc;
// 	GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 	LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
// #endif
	return false;
}

//读取事件
bool CAttemperEngineSink::OnEventTCPNetworkRead(TCP_Command Command, VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
//	LOGI("CAttemperEngineSink::OnEventTCPNetworkRead, Command.wMainCmdID="<<Command.wMainCmdID<<", Command.wSubCmdID="<<Command.wSubCmdID);
	switch (Command.wMainCmdID)
	{
	case MDM_GR_USER:		//用户命令
		{
			LOGD("CAttemperEngineSink::OnEventTCPNetworkRead, OnTCPNetworkMainUser , Command.wSubCmdID=" << Command.wSubCmdID);
			bool flag = OnTCPNetworkMainUser(Command.wSubCmdID,pData,wDataSize,dwSocketID);
// #ifdef _DEBUG
// 			HANDLE handle = GetCurrentProcess();
// 			PROCESS_MEMORY_COUNTERS pmc;
// 			GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 			LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
// #endif
			return flag;
		}
	case MDM_GR_LOGON:		//登录命令
		{
			LOGD("CAttemperEngineSink::OnEventTCPNetworkRead, OnTCPNetworkMainLogon , Command.wSubCmdID=" << Command.wSubCmdID);
			bool flag = OnTCPNetworkMainLogon(Command.wSubCmdID,pData,wDataSize,dwSocketID);
// #ifdef _DEBUG
// 			HANDLE handle = GetCurrentProcess();
// 			PROCESS_MEMORY_COUNTERS pmc;
// 			GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 			LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
// #endif
			return flag;
		}
	case MDM_GF_GAME:		//游戏命令
		{
			bool flag = OnTCPNetworkMainGame(Command.wSubCmdID,pData,wDataSize,dwSocketID);
// #ifdef _DEBUG
// 			HANDLE handle = GetCurrentProcess();
// 			PROCESS_MEMORY_COUNTERS pmc;
// 			GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 			LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
// #endif
			return flag;
		}
	case MDM_GF_FRAME:		//框架命令
		{
			LOGD("CAttemperEngineSink::OnEventTCPNetworkRead, OnTCPNetworkMainFrame , Command.wSubCmdID=" << Command.wSubCmdID);
			bool flag = OnTCPNetworkMainFrame(Command.wSubCmdID,pData,wDataSize,dwSocketID);
// #ifdef _DEBUG
// 			HANDLE handle = GetCurrentProcess();
// 			PROCESS_MEMORY_COUNTERS pmc;
// 			GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 			LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
// #endif
			return flag;
		}
	case MDM_GR_INSURE:		//银行命令
		{
			LOGD("CAttemperEngineSink::OnEventTCPNetworkRead, OnTCPNetworkMainInsure , Command.wSubCmdID=" << Command.wSubCmdID);
			bool flag = OnTCPNetworkMainInsure(Command.wSubCmdID,pData,wDataSize,dwSocketID);
// #ifdef _DEBUG
// 			HANDLE handle = GetCurrentProcess();
// 			PROCESS_MEMORY_COUNTERS pmc;
// 			GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 			LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
// #endif
			return flag;
		}
	case MDM_GR_MANAGE:		//管理命令
		{
			LOGD("CAttemperEngineSink::OnEventTCPNetworkRead, OnTCPNetworkMainManage , Command.wSubCmdID=" << Command.wSubCmdID);
			bool flag = OnTCPNetworkMainManage(Command.wSubCmdID,pData,wDataSize,dwSocketID);
// #ifdef _DEBUG
// 			HANDLE handle = GetCurrentProcess();
// 			PROCESS_MEMORY_COUNTERS pmc;
// 			GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 			LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
// #endif
			return flag;
		}
	case MDM_GR_MATCH:		//比赛命令
		{
			LOGD("CAttemperEngineSink::OnEventTCPNetworkRead, OnTCPNetworkMainMatch , Command.wSubCmdID=" << Command.wSubCmdID);
			bool flag = OnTCPNetworkMainMatch(Command.wSubCmdID,pData,wDataSize,dwSocketID);
// #ifdef _DEBUG
// 			HANDLE handle = GetCurrentProcess();
// 			PROCESS_MEMORY_COUNTERS pmc;
// 			GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
// 			LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
// #endif
			return flag;
		}
	case MDM_GR_USERCUSTOM:		// 自建房间命令
		{
			LOGD("CAttemperEngineSink::OnEventTCPNetworkRead, OnTCPNeworkUserCustom , Command.wSubCmdID=" << Command.wSubCmdID);
			bool flag = OnTCPNetworkMainUserCustom(Command.wSubCmdID, pData, wDataSize, dwSocketID);
			return flag;
		}
	}

	return false;
}

//房间消息
bool CAttemperEngineSink::SendRoomMessage(LPCTSTR lpszMessage, WORD wType)
{
	//变量定义
	CMD_CM_SystemMessage SystemMessage;
	ZeroMemory(&SystemMessage,sizeof(SystemMessage));

	//构造数据
	SystemMessage.wType=wType;
	SystemMessage.wLength=lstrlen(lpszMessage)+1;
	lstrcpyn(SystemMessage.szString,lpszMessage,CountArray(SystemMessage.szString));

	//数据属性
	WORD wHeadSize=sizeof(SystemMessage)-sizeof(SystemMessage.szString);
	WORD wSendSize=wHeadSize+CountStringBuffer(SystemMessage.szString);

	//发送数据
	m_AndroidUserManager.SendDataToClient(MDM_CM_SYSTEM,SUB_CM_SYSTEM_MESSAGE,&SystemMessage,wSendSize);
	m_pITCPNetworkEngine->SendDataBatch(MDM_CM_SYSTEM,SUB_CM_SYSTEM_MESSAGE,&SystemMessage,wSendSize,BG_COMPUTER);

	return true;
}

//游戏消息
bool CAttemperEngineSink::SendGameMessage(LPCTSTR lpszMessage, WORD wType)
{
	//变量定义
	CMD_CM_SystemMessage SystemMessage;
	ZeroMemory(&SystemMessage,sizeof(SystemMessage));

	//构造数据
	SystemMessage.wType=wType;
	SystemMessage.wLength=lstrlen(lpszMessage)+1;
	lstrcpyn(SystemMessage.szString,lpszMessage,CountArray(SystemMessage.szString));

	//数据属性
	WORD wHeadSize=sizeof(SystemMessage)-sizeof(SystemMessage.szString);
	WORD wSendSize=wHeadSize+CountStringBuffer(SystemMessage.szString);

	//发送数据
	m_AndroidUserManager.SendDataToClient(MDM_GF_FRAME,SUB_GF_SYSTEM_MESSAGE,&SystemMessage,wSendSize);
	m_pITCPNetworkEngine->SendDataBatch(MDM_GF_FRAME,SUB_GF_SYSTEM_MESSAGE,&SystemMessage,wSendSize,BG_COMPUTER);

	return true;
}

//房间消息
bool CAttemperEngineSink::SendRoomMessage(IServerUserItem * pIServerUserItem, LPCTSTR lpszMessage, WORD wType)
{
	//效验参数
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//发送数据
	if (pIServerUserItem->GetBindIndex()!=INVALID_WORD)
	{
		//变量定义
		CMD_CM_SystemMessage SystemMessage;
		ZeroMemory(&SystemMessage,sizeof(SystemMessage));

		//构造数据
		SystemMessage.wType=wType;
		SystemMessage.wLength=lstrlen(lpszMessage)+1;
		lstrcpyn(SystemMessage.szString,lpszMessage,CountArray(SystemMessage.szString));

		//变量定义
		WORD dwUserIndex=pIServerUserItem->GetBindIndex();
		tagBindParameter * pBindParameter=GetBindParameter(dwUserIndex);

		//数据属性
		WORD wHeadSize=sizeof(SystemMessage)-sizeof(SystemMessage.szString);
		WORD wSendSize=wHeadSize+CountStringBuffer(SystemMessage.szString);

		//发送数据
		if (pIServerUserItem->IsAndroidUser()==true)
		{
			//机器用户
			WORD wBindIndex=pIServerUserItem->GetBindIndex();
			tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);
			m_AndroidUserManager.SendDataToClient(pBindParameter->dwSocketID,MDM_CM_SYSTEM,SUB_CM_SYSTEM_MESSAGE,&SystemMessage,wSendSize);

			//关闭处理
			if ((wType&(SMT_CLOSE_ROOM|SMT_CLOSE_LINK))!=0)
			{
				LOGI("CAttemperEngineSink::SendRoomMessage DeleteAndroidUserItem, wType="<<wType<<", wAndroidIndex="<<LOWORD(pBindParameter->dwSocketID)<<", wRoundID="<<HIWORD(pBindParameter->dwSocketID));
				m_AndroidUserManager.DeleteAndroidUserItem(pBindParameter->dwSocketID);
			}
		}
		else
		{
			//常规用户
			WORD wBindIndex=pIServerUserItem->GetBindIndex();
			tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);
			m_pITCPNetworkEngine->SendData(pBindParameter->dwSocketID,MDM_CM_SYSTEM,SUB_CM_SYSTEM_MESSAGE,&SystemMessage,wSendSize);
		}

		return true;
	}

	return false;
}

//游戏消息
bool CAttemperEngineSink::SendGameMessage(IServerUserItem * pIServerUserItem, LPCTSTR lpszMessage, WORD wType)
{
	//效验参数
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//发送数据
	if ((pIServerUserItem->GetBindIndex()!=INVALID_WORD)&&(pIServerUserItem->IsClientReady()==true))
	{
		//变量定义
		CMD_CM_SystemMessage SystemMessage;
		ZeroMemory(&SystemMessage,sizeof(SystemMessage));

		//构造数据
		SystemMessage.wType=wType;
		SystemMessage.wLength=lstrlen(lpszMessage)+1;
		lstrcpyn(SystemMessage.szString,lpszMessage,CountArray(SystemMessage.szString));

		//变量定义
		WORD dwUserIndex=pIServerUserItem->GetBindIndex();
		tagBindParameter * pBindParameter=GetBindParameter(dwUserIndex);

		//数据属性
		WORD wHeadSize=sizeof(SystemMessage)-sizeof(SystemMessage.szString);
		WORD wSendSize=wHeadSize+CountStringBuffer(SystemMessage.szString);

		//发送数据
		if (pIServerUserItem->IsAndroidUser()==true)
		{
			//机器用户
			WORD wBindIndex=pIServerUserItem->GetBindIndex();
			tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);
			m_AndroidUserManager.SendDataToClient(pBindParameter->dwSocketID,MDM_GF_FRAME,SUB_GF_SYSTEM_MESSAGE,&SystemMessage,wSendSize);

			//关闭处理
			if ((wType&(SMT_CLOSE_ROOM|SMT_CLOSE_LINK))!=0)
			{
				LOGI("CAttemperEngineSink::SendGameMessage DeleteAndroidUserItem, wType="<<wType<<", wAndroidIndex="<<LOWORD(pBindParameter->dwSocketID)<<", wRoundID="<<HIWORD(pBindParameter->dwSocketID));
				m_AndroidUserManager.DeleteAndroidUserItem(pBindParameter->dwSocketID);
			}
		}
		else
		{
			//常规用户
			WORD wBindIndex=pIServerUserItem->GetBindIndex();
			tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);
			m_pITCPNetworkEngine->SendData(pBindParameter->dwSocketID,MDM_GF_FRAME,SUB_GF_SYSTEM_MESSAGE,&SystemMessage,wSendSize);
		}

		return true;
	}

	return false;
}

//房间消息
bool CAttemperEngineSink::SendRoomMessage(DWORD dwSocketID, LPCTSTR lpszMessage, WORD wType, bool bAndroid)
{
	//变量定义
	CMD_CM_SystemMessage SystemMessage;
	ZeroMemory(&SystemMessage,sizeof(SystemMessage));

	//构造数据
	SystemMessage.wType=wType;
	SystemMessage.wLength=lstrlen(lpszMessage)+1;
	lstrcpyn(SystemMessage.szString,lpszMessage,CountArray(SystemMessage.szString));

	//数据属性
	WORD wHeadSize=sizeof(SystemMessage)-sizeof(SystemMessage.szString);
	WORD wSendSize=wHeadSize+CountStringBuffer(SystemMessage.szString);

	//发送数据
	if (bAndroid)
	{
		//机器用户
		m_AndroidUserManager.SendDataToClient(dwSocketID,MDM_CM_SYSTEM,SUB_CM_SYSTEM_MESSAGE,&SystemMessage,wSendSize);
	}
	else
	{
		//常规用户
		m_pITCPNetworkEngine->SendData(dwSocketID,MDM_CM_SYSTEM,SUB_CM_SYSTEM_MESSAGE,&SystemMessage,wSendSize);
	}

	return true;
}

//系统消息
bool CAttemperEngineSink::SendSystemMessage(LPCTSTR lpszMessage, WORD wType)
{
	//构造消息
	CMD_GR_SendMessage SendMessage = {};
	SendMessage.cbAllRoom = TRUE;
	SendMessage.cbGame = TRUE;
	SendMessage.cbRoom = TRUE;
	lstrcpyn(SendMessage.szSystemMessage,lpszMessage,CountArray(SendMessage.szSystemMessage));
	SendMessage.wChatLength = lstrlen(SendMessage.szSystemMessage)+1;

	//发送消息
	WORD wSendSize = sizeof(SendMessage)-sizeof(SendMessage.szSystemMessage)+CountStringBuffer(SendMessage.szSystemMessage);
	SendSystemMessage(&SendMessage,wSendSize);
	return true;
};


//发送数据
bool CAttemperEngineSink::SendData(BYTE cbSendMask, WORD wMainCmdID, WORD wSubCmdID, VOID * pData, WORD wDataSize)
{
	//机器数据
	if ((cbSendMask&BG_COMPUTER)!=0)
	{
		m_AndroidUserManager.SendDataToClient(wMainCmdID,wSubCmdID,pData,wDataSize);
	}

	//用户数据
	m_pITCPNetworkEngine->SendDataBatch(wMainCmdID,wSubCmdID,pData,wDataSize,cbSendMask);

	return true;
}

//发送数据
bool CAttemperEngineSink::SendData(DWORD dwSocketID, WORD wMainCmdID, WORD wSubCmdID, VOID * pData, WORD wDataSize)
{
	//发送数据
	if (LOWORD(dwSocketID)!=INVALID_WORD)
	{
		if (LOWORD(dwSocketID)>=INDEX_ANDROID)
		{
			//机器用户
			m_AndroidUserManager.SendDataToClient(dwSocketID,wMainCmdID,wSubCmdID,pData,wDataSize);
		}
		else 
		{
			//网络用户
			m_pITCPNetworkEngine->SendData(dwSocketID,wMainCmdID,wSubCmdID,pData,wDataSize);
		}
	}

	return true;
}

//发送数据
bool CAttemperEngineSink::SendData(IServerUserItem * pIServerUserItem, WORD wMainCmdID, WORD wSubCmdID, VOID * pData, WORD wDataSize)
{
	//效验参数
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//发送数据
	if (pIServerUserItem->GetBindIndex()!=INVALID_WORD)
	{
		if (pIServerUserItem->IsAndroidUser()==true)
		{
			//机器用户
			WORD wBindIndex=pIServerUserItem->GetBindIndex();
			tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);
			m_AndroidUserManager.SendDataToClient(pBindParameter->dwSocketID,wMainCmdID,wSubCmdID,pData,wDataSize);
		}
		else
		{
			//常规用户
			WORD wBindIndex=pIServerUserItem->GetBindIndex();
			tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);
			m_pITCPNetworkEngine->SendData(pBindParameter->dwSocketID,wMainCmdID,wSubCmdID,pData,wDataSize);
		}

		return true;
	}

	return false;
}

//用户积分
bool CAttemperEngineSink::OnEventUserItemScore(IServerUserItem * pIServerUserItem, BYTE cbReason)
{
	
	//效验参数
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	LOGI("CAttemperEngineSink::OnEventUserItemScore, NickName="<<pIServerUserItem->GetNickName()<<", cbReason="<<cbReason);

	//变量定义
	CMD_GR_UserScore UserScore;
	tagUserInfo * pUserInfo=pIServerUserItem->GetUserInfo();

	//构造数据
	UserScore.dwUserID=pUserInfo->dwUserID;
	UserScore.UserScore.dwWinCount=pUserInfo->dwWinCount;
	UserScore.UserScore.dwLostCount=pUserInfo->dwLostCount;
	UserScore.UserScore.dwDrawCount=pUserInfo->dwDrawCount;
	UserScore.UserScore.dwFleeCount=pUserInfo->dwFleeCount;
	UserScore.UserScore.dwUserMedal=pUserInfo->dwUserMedal;
	UserScore.UserScore.dwExperience=pUserInfo->dwExperience;
	UserScore.UserScore.lLoveLiness=pUserInfo->lLoveLiness;

	//构造积分
	UserScore.UserScore.lGrade=pUserInfo->lGrade;
	UserScore.UserScore.lInsure=pUserInfo->lInsure;

	//构造积分
	if (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM)
	{
		tagVariationInfo UserRecordInfo;
		pIServerUserItem->QueryRecordInfo(UserRecordInfo);
		UserScore.UserScore.lScore = UserRecordInfo.lScore;
	}
	else
	{
		UserScore.UserScore.lScore = pUserInfo->lScore;
	}
	UserScore.UserScore.lScore+=pIServerUserItem->GetTrusteeScore();
	UserScore.UserScore.lScore+=pIServerUserItem->GetFrozenedScore();

	//发送数据
	SendData(BG_COMPUTER,MDM_GR_USER,SUB_GR_USER_SCORE,&UserScore,sizeof(UserScore));
//	LOGI("CAttemperEngineSink::OnEventUserItemScore, NickName="<<pIServerUserItem->GetNickName()<<", SendData CMD_GR_UserScore");

	//变量定义
	CMD_GR_MobileUserScore MobileUserScore;

	//构造数据
	MobileUserScore.dwUserID=pUserInfo->dwUserID;
	MobileUserScore.UserScore.dwWinCount=pUserInfo->dwWinCount;
	MobileUserScore.UserScore.dwLostCount=pUserInfo->dwLostCount;
	MobileUserScore.UserScore.dwDrawCount=pUserInfo->dwDrawCount;
	MobileUserScore.UserScore.dwFleeCount=pUserInfo->dwFleeCount;
	MobileUserScore.UserScore.dwExperience=pUserInfo->dwExperience;

	//构造积分
	MobileUserScore.UserScore.lScore=pUserInfo->lScore;
	MobileUserScore.UserScore.lScore+=pIServerUserItem->GetTrusteeScore();
	MobileUserScore.UserScore.lScore+=pIServerUserItem->GetFrozenedScore();

	//发送数据
	SendDataBatchToMobileUser(pIServerUserItem->GetTableID(),MDM_GR_USER,SUB_GR_USER_SCORE,&MobileUserScore,sizeof(MobileUserScore));
//	LOGI("CAttemperEngineSink::OnEventUserItemScore, NickName="<<pIServerUserItem->GetNickName()<<", SendDataBatchToMobileUser CMD_GR_MobileUserScore");

	//即时写分
	if ((CServerRule::IsImmediateWriteScore(m_pGameServiceOption->dwServerRule)==true)&&(pIServerUserItem->IsVariation()==true))
	{
//		LOGI("CAttemperEngineSink::OnEventUserItemScore, NickName="<<pIServerUserItem->GetNickName()<<", IsImmediateWriteScore &&  IsVariation");
		//变量定义
		DBR_GR_WriteGameScore WriteGameScore;
		ZeroMemory(&WriteGameScore,sizeof(WriteGameScore));

		//用户信息
		WriteGameScore.dwUserID=pIServerUserItem->GetUserID();
		WriteGameScore.dwDBQuestID=pIServerUserItem->GetDBQuestID();
		WriteGameScore.dwClientAddr=pIServerUserItem->GetClientAddr();
		WriteGameScore.dwInoutIndex=pIServerUserItem->GetInoutIndex();

		// 获取桌号，椅子号(有记录的地方，不需要这里记录)
// 		WriteGameScore.wTableID = pIServerUserItem->GetTableID();
// 		WriteGameScore.wChairID = pIServerUserItem->GetChairID();

		//提取积分
		pIServerUserItem->DistillVariation(WriteGameScore.VariationInfo);

        WriteGameScore.VariationInfo.dwExperience=0;
        WriteGameScore.VariationInfo.dwDrawCount=0;
        WriteGameScore.VariationInfo.dwFleeCount=0;
        WriteGameScore.VariationInfo.dwWinCount=0;
        WriteGameScore.VariationInfo.dwLostCount=0;

		// 得到socketid，用来得到回复的消息
		WORD wBindIndex=pIServerUserItem->GetBindIndex();
		tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);
		DWORD dwSocketID = 0;
		if (pBindParameter != NULL)
		{
			dwSocketID = pBindParameter->dwSocketID;
		}
		else
		{
			LOGW("CAttemperEngineSink::OnEventUserItemScore, NickName="<<pIServerUserItem->GetNickName()<<", wBindIndex="<<wBindIndex<<", pBindParameter==NULL");
		}

		//投递请求
		m_pIDBCorrespondManager->PostDataBaseRequest(WriteGameScore.dwUserID,DBR_GR_WRITE_GAME_SCORE,dwSocketID,&WriteGameScore,sizeof(WriteGameScore), TRUE);
	}

	//通知桌子
	if(pIServerUserItem->GetTableID()!=INVALID_TABLE)
	{
		CTableFrame* pTableFrame = m_TableFrameArray[pIServerUserItem->GetTableID()];
		if (pTableFrame != NULL)
		{
			pTableFrame->OnUserScroeNotify(pIServerUserItem->GetChairID(),pIServerUserItem,cbReason);
		}
		else
		{
			LOGW("CAttemperEngineSink::OnEventUserItemScore, NickName="<<pIServerUserItem->GetNickName()<<", pTableFrame==NULL, GetTableID="<<pIServerUserItem->GetTableID());
		}
	}

	return true;
}

//用户状态
bool CAttemperEngineSink::OnEventUserItemStatus(IServerUserItem * pIServerUserItem, WORD wOldTableID, WORD wOldChairID)
{
	//效验参数
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//变量定义
	CMD_GR_UserStatus UserStatus;
	ZeroMemory(&UserStatus,sizeof(UserStatus));

	//构造数据
	UserStatus.dwUserID=pIServerUserItem->GetUserID();
	UserStatus.UserStatus.wTableID=pIServerUserItem->GetTableID();
	UserStatus.UserStatus.wChairID=pIServerUserItem->GetChairID();
	UserStatus.UserStatus.cbUserStatus=pIServerUserItem->GetUserStatus();

	//修改信息
	if(pIServerUserItem->GetUserStatus()==US_SIT && pIServerUserItem->IsMobileUser())
	{
		//定义变量
		WORD wTagerDeskPos = pIServerUserItem->GetMobileUserDeskPos();
		WORD wTagerDeskCount = pIServerUserItem->GetMobileUserDeskCount();

		WORD wTableCount = wTagerDeskPos+wTagerDeskCount-1;
		if(pIServerUserItem->GetTableID() > wTableCount && wTagerDeskCount>0)
		{
			//定义变量
			WORD wNewDeskPos = (pIServerUserItem->GetTableID()/wTagerDeskCount)*wTagerDeskCount;
			WORD wMaxDeskPos = m_pGameServiceOption->wTableCount-wTagerDeskCount;

			//数量效验
			if(wNewDeskPos > wMaxDeskPos) wNewDeskPos = wMaxDeskPos;

			//修改信息
			pIServerUserItem->SetMobileUserDeskPos(wNewDeskPos);
		}
	}

	//发送数据
	SendData(BG_COMPUTER,MDM_GR_USER,SUB_GR_USER_STATUS,&UserStatus,sizeof(UserStatus));
	if(pIServerUserItem->GetUserStatus()==US_SIT)
		SendDataBatchToMobileUser(pIServerUserItem->GetTableID(),MDM_GR_USER,SUB_GR_USER_STATUS,&UserStatus,sizeof(UserStatus));
	else
		SendDataBatchToMobileUser(wOldTableID,MDM_GR_USER,SUB_GR_USER_STATUS,&UserStatus,sizeof(UserStatus));

	//离开判断
	if (pIServerUserItem->GetUserStatus()==US_NULL)
	{
		//获取绑定
		WORD wBindIndex=pIServerUserItem->GetBindIndex();
		tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);

		//绑带处理
		if (pBindParameter!=NULL)
		{
			//绑定处理
			if (pBindParameter->pIServerUserItem==pIServerUserItem)
			{
				pBindParameter->pIServerUserItem=NULL;
			}

			//中断网络
			if (pBindParameter->dwSocketID!=0L)
			{
				if (LOWORD(pBindParameter->dwSocketID)>=INDEX_ANDROID)
				{
					LOGI("CAttemperEngineSink::OnEventUserItemStatus, m_wAndroidIndex="<<LOWORD(pBindParameter->dwSocketID)<<", m_wRoundID="<<HIWORD(pBindParameter->dwSocketID));
					m_AndroidUserManager.DeleteAndroidUserItem(pBindParameter->dwSocketID);
				}
				else
				{
					m_pITCPNetworkEngine->ShutDownSocket(pBindParameter->dwSocketID);
				}
			}
		}

		//离开处理
		OnEventUserLogout(pIServerUserItem,0L);
	}
	return true;
}

//用户权限
bool CAttemperEngineSink::OnEventUserItemRight(IServerUserItem *pIServerUserItem, DWORD dwAddRight, DWORD dwRemoveRight,bool bGameRight)
{
	//效验参数
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//变量定义
	tagUserInfo * pUserInfo=pIServerUserItem->GetUserInfo();

	DBR_GR_ManageUserRight ManageUserRight= {0};
	ManageUserRight.dwUserID = pUserInfo->dwUserID;
	ManageUserRight.dwAddRight = dwAddRight;
	ManageUserRight.dwRemoveRight = dwRemoveRight;
	ManageUserRight.bGameRight=bGameRight;

	//发送请求
	m_pIDBCorrespondManager->PostDataBaseRequest(ManageUserRight.dwUserID,DBR_GR_MANAGE_USER_RIGHT,0,&ManageUserRight,sizeof(ManageUserRight));

	return true;
}

//登录成功
bool CAttemperEngineSink::OnDBLogonSuccess(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//变量定义
	WORD wBindIndex=LOWORD(dwContextID);
	tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);
	DBO_GR_LogonSuccess * pDBOLogonSuccess=(DBO_GR_LogonSuccess *)pData;

	//废弃判断
	if ((pBindParameter->pIServerUserItem!=NULL)||(pBindParameter->dwSocketID!=dwContextID))
	{
		//错误断言
		ASSERT(FALSE);

		//解除锁定
		PerformUnlockScore(pDBOLogonSuccess->dwUserID,pDBOLogonSuccess->dwInoutIndex,LER_NORMAL);

		return true;
	}

	//变量定义
	bool bAndroidUser=(wBindIndex>=INDEX_ANDROID);
	bool bMobileUser=(pBindParameter->cbClientKind==CLIENT_KIND_MOBILE);

	//被踢判断
	DWORD dwKickTime;
	if(m_KickUserItemMap.Lookup(pDBOLogonSuccess->dwUserID,dwKickTime)==TRUE)
	{
		//时效判断
		DWORD dwCurrTime = (DWORD)time(NULL);
		if(dwKickTime+VALID_TIME_KICK_BY_MANAGER > dwCurrTime)
		{
			//发送失败
			SendLogonFailure(TEXT("您已被管理员请出房间,1小时之内不能进入！"),0,pBindParameter->dwSocketID);

			//解除锁定
			PerformUnlockScore(pDBOLogonSuccess->dwUserID,pDBOLogonSuccess->dwInoutIndex,LER_SERVER_CONDITIONS);

			return true;
		}
		else
		{
			//移除玩家
			m_KickUserItemMap.RemoveKey(pDBOLogonSuccess->dwUserID);
		}
	}

	//切换判断
	IServerUserItem * pIServerUserItem=m_ServerUserManager.SearchUserItem(pDBOLogonSuccess->dwUserID);
	if (pIServerUserItem!=NULL)
	{
		//切换用户
		SwitchUserItemConnect(pIServerUserItem,pDBOLogonSuccess->szMachineID,wBindIndex,pDBOLogonSuccess->cbDeviceType,pDBOLogonSuccess->wBehaviorFlags,pDBOLogonSuccess->wPageTableCount);

		//解除锁定
		PerformUnlockScore(pDBOLogonSuccess->dwUserID,pDBOLogonSuccess->dwInoutIndex,LER_USER_IMPACT);

		return true;
	}

	//维护判断
	if ((CServerRule::IsForfendRoomEnter(m_pGameServiceOption->dwServerRule)==true)&&(pDBOLogonSuccess->cbMasterOrder==0))
	{
		//发送失败
		SendLogonFailure(TEXT("抱歉，由于系统维护的原因，当前游戏房间禁止用户进入！"),0,pBindParameter->dwSocketID);

		//解除锁定
		PerformUnlockScore(pDBOLogonSuccess->dwUserID,pDBOLogonSuccess->dwInoutIndex,LER_SYSTEM);

		return true;
	}

	//查找机器
	if (bAndroidUser==true)
	{
		//查找机器
		DWORD dwUserID=pDBOLogonSuccess->dwUserID;
		IAndroidUserItem * pIAndroidUserItem=m_AndroidUserManager.SearchAndroidUserItem(dwUserID,dwContextID);

		//修改积分
		if (pIAndroidUserItem!=NULL)
		{
			//获取配置
			tagAndroidParameter * pAndroidParameter=pIAndroidUserItem->GetAndroidParameter();

			//调整积分
			if ((pAndroidParameter->lMinTakeScore!=0L)&&(pAndroidParameter->lMaxTakeScore!=0L))
			{
				//变量定义
				SCORE lMinTakeScore=(SCORE)pAndroidParameter->lMinTakeScore;
				SCORE lMaxTakeScore=(SCORE)__max(pAndroidParameter->lMaxTakeScore,pAndroidParameter->lMinTakeScore);

				//调整积分
				if ((lMaxTakeScore-lMinTakeScore)>0L)
				{
					SCORE lTakeScore = (lMaxTakeScore-lMinTakeScore)/10;
					pDBOLogonSuccess->lScore=(SCORE)(lMinTakeScore+(rand()%10)*lTakeScore+rand()%lTakeScore);
				}
				else
				{
					pDBOLogonSuccess->lScore=(SCORE)lMaxTakeScore;
				}
			}
		}

		LOGI(TEXT("CAttemperEngineSink::OnDBLogonSuccess, 机器人"<<dwUserID<<TEXT("分数调整为")<<pDBOLogonSuccess->lScore))
	}

	//最低分数
	if ((m_pGameServiceOption->lMinEnterScore!=0L)&&(pDBOLogonSuccess->lScore<m_pGameServiceOption->lMinEnterScore))
	{
		//发送失败
		TCHAR szMsg[128]=TEXT("");
		_sntprintf(szMsg,CountArray(szMsg), TEXT("抱歉，您的游戏游戏币低于当前游戏房间的最低进入游戏币%I64d，不能进入当前游戏房间！"), m_pGameServiceOption->lMinEnterScore);
		SendLogonFailure(szMsg,0,pBindParameter->dwSocketID);

		//解除锁定
		PerformUnlockScore(pDBOLogonSuccess->dwUserID,pDBOLogonSuccess->dwInoutIndex,LER_SERVER_CONDITIONS);

		return true;
	}

	//最高分数
	if ((m_pGameServiceOption->lMaxEnterScore!=0L)&&(pDBOLogonSuccess->lScore>m_pGameServiceOption->lMaxEnterScore))
	{
		//发送失败
		TCHAR szMsg[128]=TEXT("");
		_sntprintf(szMsg,CountArray(szMsg), TEXT("抱歉，您的游戏游戏币高于当前游戏房间的最高进入游戏币%I64d，不能进入当前游戏房间！"), m_pGameServiceOption->lMaxEnterScore);
		SendLogonFailure(szMsg,0,pBindParameter->dwSocketID);

		//解除锁定
		PerformUnlockScore(pDBOLogonSuccess->dwUserID,pDBOLogonSuccess->dwInoutIndex,LER_SERVER_CONDITIONS);

		return true;
	}

	//会员判断
	if(m_pGameServiceOption->cbMinEnterMember != 0 && pDBOLogonSuccess->cbMemberOrder < m_pGameServiceOption->cbMinEnterMember &&(pDBOLogonSuccess->cbMasterOrder==0))
	{
		//发送失败
		SendLogonFailure(TEXT("抱歉，您的会员级别低于当前游戏房间的最低进入会员条件，不能进入当前游戏房间！"),0,pBindParameter->dwSocketID);

		//解除锁定
		PerformUnlockScore(pDBOLogonSuccess->dwUserID,pDBOLogonSuccess->dwInoutIndex,LER_SERVER_CONDITIONS);

		return true;
	}

	//会员判断
	if(m_pGameServiceOption->cbMaxEnterMember != 0 && pDBOLogonSuccess->cbMemberOrder > m_pGameServiceOption->cbMaxEnterMember &&(pDBOLogonSuccess->cbMasterOrder==0))
	{
		//发送失败
		SendLogonFailure(TEXT("抱歉，您的会员级别高于当前游戏房间的最高进入会员条件，不能进入当前游戏房间！"),0,pBindParameter->dwSocketID);

		//解除锁定
		PerformUnlockScore(pDBOLogonSuccess->dwUserID,pDBOLogonSuccess->dwInoutIndex,LER_SERVER_CONDITIONS);

		return true;
	}

	//满人判断
	WORD wMaxPlayer=m_pGameServiceOption->wMaxPlayer;
	DWORD dwOnlineCount=m_ServerUserManager.GetUserItemCount();
	if ((pDBOLogonSuccess->cbMemberOrder==0)&&(pDBOLogonSuccess->cbMasterOrder==0)&&(dwOnlineCount>(DWORD)(wMaxPlayer-RESERVE_USER_COUNT)))
	{
		//发送失败
		SendLogonFailure(TEXT("抱歉，由于此房间已经人满，普通玩家不能继续进入了！"),0,pBindParameter->dwSocketID);

		//解除锁定
		PerformUnlockScore(pDBOLogonSuccess->dwUserID,pDBOLogonSuccess->dwInoutIndex,LER_SERVER_FULL);

		return true;
	}

	//用户变量
	tagUserInfo UserInfo;
	tagUserInfoPlus UserInfoPlus;
	ZeroMemory(&UserInfo,sizeof(UserInfo));
	ZeroMemory(&UserInfoPlus,sizeof(UserInfoPlus));

	//属性资料
	UserInfo.wFaceID=pDBOLogonSuccess->wFaceID;
	UserInfo.dwUserID=pDBOLogonSuccess->dwUserID;
	UserInfo.dwGameID=pDBOLogonSuccess->dwGameID;
	UserInfo.dwGroupID=pDBOLogonSuccess->dwGroupID;
	UserInfo.dwCustomID=pDBOLogonSuccess->dwCustomID;
	UserInfo.dwAreaID = pDBOLogonSuccess->dwAreaID;
	lstrcpyn(UserInfo.szNickName,pDBOLogonSuccess->szNickName,CountArray(UserInfo.szNickName));

	//用户资料
	UserInfo.cbGender=pDBOLogonSuccess->cbGender;
	UserInfo.cbMemberOrder=pDBOLogonSuccess->cbMemberOrder;
	UserInfo.cbMasterOrder=pDBOLogonSuccess->cbMasterOrder;
	lstrcpyn(UserInfo.szGroupName,pDBOLogonSuccess->szGroupName,CountArray(UserInfo.szGroupName));
	lstrcpyn(UserInfo.szUnderWrite,pDBOLogonSuccess->szUnderWrite,CountArray(UserInfo.szUnderWrite));

	//状态设置
	UserInfo.cbUserStatus=US_FREE;
	UserInfo.wTableID=INVALID_TABLE;
	UserInfo.wChairID=INVALID_CHAIR;

	//积分信息
	UserInfo.lScore=pDBOLogonSuccess->lScore;
	UserInfo.lGrade=pDBOLogonSuccess->lGrade;
	UserInfo.lInsure=pDBOLogonSuccess->lInsure;
	UserInfo.dwWinCount=pDBOLogonSuccess->dwWinCount;
	UserInfo.dwLostCount=pDBOLogonSuccess->dwLostCount;
	UserInfo.dwDrawCount=pDBOLogonSuccess->dwDrawCount;
	UserInfo.dwFleeCount=pDBOLogonSuccess->dwFleeCount;
	UserInfo.dwUserMedal=pDBOLogonSuccess->dwUserMedal;
	UserInfo.dwExperience=pDBOLogonSuccess->dwExperience;
	UserInfo.lLoveLiness=pDBOLogonSuccess->lLoveLiness;

	//登录信息
	UserInfoPlus.dwLogonTime=(DWORD)time(NULL);
	UserInfoPlus.dwInoutIndex=pDBOLogonSuccess->dwInoutIndex;

	//用户权限
	UserInfoPlus.dwUserRight=pDBOLogonSuccess->dwUserRight;
	UserInfoPlus.dwMasterRight=pDBOLogonSuccess->dwMasterRight;

	//辅助变量
	UserInfoPlus.bMobileUser=bMobileUser;
	UserInfoPlus.bAndroidUser=bAndroidUser;
#ifndef _READ_TABLE_OPTION_
	UserInfoPlus.lRestrictScore=m_pGameServiceOption->lRestrictScore;
#else
	// 此处需要调整,需要根据玩家坐下时的桌子,选择最大的限制
	// 而不是在玩家登录时确定最大的限制
//	UserInfoPlus.lRestrictScore = m_pGameServiceOption->lRestrictScore;
#endif
	lstrcpyn(UserInfoPlus.szPassword,pDBOLogonSuccess->szPassword,CountArray(UserInfoPlus.szPassword));

	//连接信息
	UserInfoPlus.wBindIndex=wBindIndex;
	UserInfoPlus.dwClientAddr=pBindParameter->dwClientAddr;
	lstrcpyn(UserInfoPlus.szMachineID,pDBOLogonSuccess->szMachineID,CountArray(UserInfoPlus.szMachineID));

	//激活用户
	m_ServerUserManager.InsertUserItem(&pIServerUserItem,UserInfo,UserInfoPlus);

	//错误判断
	if (pIServerUserItem==NULL)
	{
		//错误断言
		ASSERT(FALSE);

		//解除锁定
		PerformUnlockScore(pDBOLogonSuccess->dwUserID,pDBOLogonSuccess->dwInoutIndex,LER_SERVER_FULL);

		//断开用户
		if (bAndroidUser==true)
		{
			LOGI("CAttemperEngineSink::OnDBLogonSuccess pIServerUserItem==NULL DeleteAndroidUserItem, m_wAndroidIndex="<<LOWORD(dwContextID)<<", m_wRoundID="<<HIWORD(dwContextID));
			m_AndroidUserManager.DeleteAndroidUserItem(dwContextID);
		}
		else
		{
			m_pITCPNetworkEngine->ShutDownSocket(dwContextID);
		}

		return true;
	}

	// 设置玩家总输赢
	pIServerUserItem->SetTotalWin(pDBOLogonSuccess->lTotalWin);
	pIServerUserItem->SetTotalLose(pDBOLogonSuccess->lTotalLose);

	// 设置玩家运气分以及buff等数据


	//设置用户
	pBindParameter->pIServerUserItem=pIServerUserItem;

	//修改参数
	if(pIServerUserItem->IsMobileUser())
	{
		SetMobileUserParameter(pIServerUserItem,pDBOLogonSuccess->cbDeviceType,pDBOLogonSuccess->wBehaviorFlags,pDBOLogonSuccess->wPageTableCount);
	}

	//登录事件
	OnEventUserLogon(pIServerUserItem,false);

	//汇总用户
	if (m_bCollectUser==true)
	{
		//变量定义
		CMD_CS_C_UserEnter UserEnter;
		ZeroMemory(&UserEnter,sizeof(UserEnter));

		//设置变量
		UserEnter.dwUserID=pIServerUserItem->GetUserID();
		UserEnter.dwGameID=pIServerUserItem->GetGameID();
		lstrcpyn(UserEnter.szNickName,pIServerUserItem->GetNickName(),CountArray(UserEnter.szNickName));

		//辅助信息
		UserEnter.cbGender=pIServerUserItem->GetGender();
		UserEnter.cbMemberOrder=pIServerUserItem->GetMemberOrder();
		UserEnter.cbMasterOrder=pIServerUserItem->GetMasterOrder();

		//发送消息
		ASSERT(m_pITCPSocketService!=NULL);
		m_pITCPSocketService->SendData(MDM_CS_USER_COLLECT,SUB_CS_C_USER_ENTER,&UserEnter,sizeof(UserEnter));
	}

	//广场赛记录
	for (CSquareManager::iterator iter=m_SquareManager.begin(); iter!=m_SquareManager.end(); iter++)
	{
		if(!iter->second.m_bStartSquare)continue;

		if(iter->second.m_dwSquareRemain>=5)
		{
			map<DWORD, tagSQUARERANK>::iterator iterRank = iter->second.m_SquareScoreMap.find(pDBOLogonSuccess->dwUserID);
			if(iterRank==iter->second.m_SquareScoreMap.end())
			{
				tagSQUARERANK SquareRank;
				SquareRank.lScore=0;
				SquareRank.wResult=0;
				SquareRank.dwUserID=pDBOLogonSuccess->dwUserID;
				SquareRank.dwEnterTime=(DWORD)(CTime::GetCurrentTime().GetTime());
				lstrcpyn(SquareRank.szNickName,pIServerUserItem->GetNickName(),CountArray(SquareRank.szNickName));
				iter->second.AddRank(pIServerUserItem->GetUserID(),SquareRank);
			}

			if (!pIServerUserItem->IsAndroidUser())
			{
				iter->second.SendSquareEnter(pIServerUserItem->GetUserID());
				iter->second.SendTopPlayers(pIServerUserItem->GetUserID());
			}
		}

	}

#ifdef _DEBUG
	HANDLE handle = GetCurrentProcess();
	PROCESS_MEMORY_COUNTERS pmc;
	GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
	LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
#endif

	return true;
}

//登录失败
bool CAttemperEngineSink::OnDBLogonFailure(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//判断在线
	tagBindParameter * pBindParameter=GetBindParameter(LOWORD(dwContextID));
	if ((pBindParameter->dwSocketID!=dwContextID)||(pBindParameter->pIServerUserItem!=NULL)) return true;

	//发送错误
	DBO_GR_LogonFailure * pLogonFailure=(DBO_GR_LogonFailure *)pData;
	SendLogonFailure(pLogonFailure->szDescribeString,pLogonFailure->lResultCode,dwContextID);

	//断开连接
	if (LOWORD(dwContextID)>=INDEX_ANDROID)
	{
		LOGI("CAttemperEngineSink::OnDBLogonFailure DeleteAndroidUserItem, m_wAndroidIndex="<<LOWORD(dwContextID)<<", m_wRoundID="<<HIWORD(dwContextID));
		m_AndroidUserManager.DeleteAndroidUserItem(dwContextID);
	}
// 	else
// 	{
// 		m_pITCPNetworkEngine->ShutDownSocket(dwContextID);
// 	}

	return true;
}

//配置信息
bool CAttemperEngineSink::OnDBGameParameter(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//效验参数
	ASSERT(wDataSize==sizeof(DBO_GR_GameParameter));
	if (wDataSize!=sizeof(DBO_GR_GameParameter)) return false;

	//变量定义
	DBO_GR_GameParameter * pGameParameter=(DBO_GR_GameParameter *)pData;

	//汇率信息
	m_pGameParameter->wMedalRate=pGameParameter->wMedalRate;
	m_pGameParameter->wRevenueRate=pGameParameter->wRevenueRate;

	//版本信息
	m_pGameParameter->dwClientVersion=pGameParameter->dwClientVersion;
//	m_pGameParameter->dwAppClientVersion = pGameParameter->dwAppClientVersion;
	m_pGameParameter->dwServerVersion=pGameParameter->dwServerVersion;

	//版本效验
	if (VERSION_EFFICACY==TRUE)
	{
		//版本判断
		bool bVersionInvalid=false;
		if (m_pGameParameter->dwClientVersion!=m_pGameServiceAttrib->dwClientVersion) bVersionInvalid=true;
//		if (m_pGameParameter->dwAppClientVersion != m_pGameServiceAttrib->dwAppClientVersion) bVersionInvalid = true;
		if (m_pGameParameter->dwServerVersion!=m_pGameServiceAttrib->dwServerVersion) bVersionInvalid=true;

		//提示信息
		if (bVersionInvalid==true)
		{
			CTraceService::TraceString(TEXT("平台数据库服务组件版本注册信息与当前组件版本信息不一致"),TraceLevel_Warning);
			LOGI( TEXT("CAttemperEngineSink::OnDBGameParameter, 平台数据库服务组件版本注册信息与当前组件版本信息不一致") );
		}
	}

	return true;
}

//列表信息
bool CAttemperEngineSink::OnDBGameColumnInfo(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//变量定义
	DBO_GR_GameColumnInfo * pGameColumnInfo=(DBO_GR_GameColumnInfo *)pData;
	WORD wHeadSize=sizeof(DBO_GR_GameColumnInfo)-sizeof(pGameColumnInfo->ColumnItemInfo);

	//效验参数
	ASSERT((wDataSize>=wHeadSize)&&(wDataSize==(wHeadSize+pGameColumnInfo->cbColumnCount*sizeof(pGameColumnInfo->ColumnItemInfo[0]))));
	if ((wDataSize<wHeadSize)||(wDataSize!=(wHeadSize+pGameColumnInfo->cbColumnCount*sizeof(pGameColumnInfo->ColumnItemInfo[0])))) return false;

	//数据处理
	if (pGameColumnInfo->cbColumnCount==0)
	{
		//默认列表
	}
	else
	{
		//拷贝数据
		m_DataConfigColumn.cbColumnCount=pGameColumnInfo->cbColumnCount;
		CopyMemory(m_DataConfigColumn.ColumnItem,pGameColumnInfo->ColumnItemInfo,pGameColumnInfo->cbColumnCount*sizeof(pGameColumnInfo->ColumnItemInfo[0]));
	}

	return true;
}

//机器信息
bool CAttemperEngineSink::OnDBGameAndroidInfo(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
//	LOGI("CAttemperEngineSink::OnDBGameAndroidInfo, dwContextID="<<dwContextID);
	if (dwContextID == 0xFFFF)
	{
		m_AndroidUserManager.ResetAndroidStock();
		return true;
	}
	//变量定义
	DBO_GR_GameAndroidInfo * pGameAndroidInfo=(DBO_GR_GameAndroidInfo *)pData;
//	WORD wHeadSize=sizeof(DBO_GR_GameAndroidInfo)-sizeof(pGameAndroidInfo->AndroidParameter);

	//效验参数
//	ASSERT((wDataSize>=wHeadSize)&&(wDataSize==(wHeadSize+pGameAndroidInfo->wAndroidCount*sizeof(pGameAndroidInfo->AndroidParameter[0]))));
//	if ((wDataSize<wHeadSize)||(wDataSize!=(wHeadSize+pGameAndroidInfo->wAndroidCount*sizeof(pGameAndroidInfo->AndroidParameter[0])))) return false;

//	LOGI("CAttemperEngineSink::OnDBGameAndroidInfo, lResultCode="<<pGameAndroidInfo->lResultCode);
	//设置机器
	if (pGameAndroidInfo->lResultCode==DB_SUCCESS)
	{
//		m_AndroidUserManager.SetAndroidStock(pGameAndroidInfo->AndroidParameter,pGameAndroidInfo->wAndroidCount);
		m_AndroidUserManager.SetAndroidStock(pGameAndroidInfo->AndroidParameter);
	}

	return true;
}

//道具信息
bool CAttemperEngineSink::OnDBGamePropertyInfo(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//变量定义
	DBO_GR_GamePropertyInfo * pGamePropertyInfo=(DBO_GR_GamePropertyInfo *)pData;
	WORD wHeadSize=sizeof(DBO_GR_GamePropertyInfo)-sizeof(pGamePropertyInfo->PropertyInfo);

	//效验参数
	ASSERT((wDataSize>=wHeadSize)&&(wDataSize==(wHeadSize+pGamePropertyInfo->cbPropertyCount*sizeof(pGamePropertyInfo->PropertyInfo[0]))));
	if ((wDataSize<wHeadSize)||(wDataSize!=(wHeadSize+pGamePropertyInfo->cbPropertyCount*sizeof(pGamePropertyInfo->PropertyInfo[0])))) return false;

	//获取状态
	CServiceUnits * pServiceUnits=CServiceUnits::g_pServiceUnits;
	enServiceStatus ServiceStatus=pServiceUnits->GetServiceStatus();

	//设置道具
	if (pGamePropertyInfo->lResultCode==DB_SUCCESS)
	{
		//设置管理
		m_GamePropertyManager.SetGamePropertyInfo(pGamePropertyInfo->PropertyInfo,pGamePropertyInfo->cbPropertyCount);

		//拷贝数据
		m_DataConfigProperty.cbPropertyCount=pGamePropertyInfo->cbPropertyCount;
		CopyMemory(m_DataConfigProperty.PropertyInfo,pGamePropertyInfo->PropertyInfo,pGamePropertyInfo->cbPropertyCount*sizeof(pGamePropertyInfo->PropertyInfo[0]));
	}

	//事件通知
	if (ServiceStatus!=ServiceStatus_Service)
	{
		CP_ControlResult ControlResult;
		ControlResult.cbSuccess=ER_SUCCESS;
		SendUIControlPacket(UI_SERVICE_CONFIG_RESULT,&ControlResult,sizeof(ControlResult));
	}

	return true;
}

//银行信息
bool CAttemperEngineSink::OnDBUserInsureInfo(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//判断在线
	tagBindParameter * pBindParameter=GetBindParameter(LOWORD(dwContextID));
	if ((pBindParameter->dwSocketID!=dwContextID)||(pBindParameter->pIServerUserItem==NULL)) return true;

	//获取用户
	ASSERT(GetBindUserItem(LOWORD(dwContextID))!=NULL);
	IServerUserItem * pIServerUserItem=GetBindUserItem(LOWORD(dwContextID));

	//变量定义
	DBO_GR_UserInsureInfo * pUserInsureInfo=(DBO_GR_UserInsureInfo *)pData;

	//变量定义
	CMD_GR_S_UserInsureInfo UserInsureInfo;
	ZeroMemory(&UserInsureInfo,sizeof(UserInsureInfo));

	//构造数据
	UserInsureInfo.cbActivityGame=pUserInsureInfo->cbActivityGame;
	UserInsureInfo.wRevenueTake=pUserInsureInfo->wRevenueTake;
	UserInsureInfo.wRevenueTransfer=pUserInsureInfo->wRevenueTransfer;
	UserInsureInfo.wServerID=pUserInsureInfo->wServerID;
	UserInsureInfo.lUserInsure=pUserInsureInfo->lUserInsure;
	UserInsureInfo.lUserScore+=pIServerUserItem->GetUserScore();
	UserInsureInfo.lUserScore+=pIServerUserItem->GetTrusteeScore();
	UserInsureInfo.lUserScore+=pIServerUserItem->GetFrozenedScore();
	UserInsureInfo.lTransferPrerequisite=pUserInsureInfo->lTransferPrerequisite;

	//发送数据
//	m_pITCPNetworkEngine->SendData(dwContextID,MDM_GR_INSURE,SUB_GR_USER_INSURE_INFO,&UserInsureInfo,sizeof(UserInsureInfo));
	SendData(pIServerUserItem,MDM_GR_INSURE,SUB_GR_USER_INSURE_INFO,&UserInsureInfo,sizeof(UserInsureInfo));

	return true;
}
bool CAttemperEngineSink::OnDBUserTransRecord(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
    ASSERT(wDataSize==sizeof(DBR_GR_UserTransRecord));
	if (wDataSize!=sizeof(DBR_GR_UserTransRecord)) return false;

	DBR_GR_UserTransRecord *pTransRecord=(DBR_GR_UserTransRecord *)pData;
    
	CMD_GR_RealTransRecord RealTransRecord;
	ZeroMemory(&RealTransRecord,sizeof(RealTransRecord));
	RealTransRecord.dwGameID = pTransRecord->dwGameID;
	_tcscpy(RealTransRecord.szSourceAccount,pTransRecord->szSourceAccount);
	_tcscpy(RealTransRecord.szTargetAccounts,pTransRecord->szTargetAccounts);
	_tcscpy(RealTransRecord.szTime,pTransRecord->szTime);
	RealTransRecord.lSwapScore = pTransRecord->lSwapScore;
    RealTransRecord.lRevenue=pTransRecord->lRevenue;
    RealTransRecord.bOver = pTransRecord->bOver;
	RealTransRecord.dwTargetID = pTransRecord->dwTargetID;

	m_pITCPNetworkEngine->SendData(dwContextID,MDM_GR_INSURE,SUB_GR_TRANS_RECORD,&RealTransRecord,sizeof(RealTransRecord));
    return true;
}
//银行成功
bool CAttemperEngineSink::OnDBUserInsureSuccess(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
//	LOGI(_T("CAttemperEngineSink::OnDBUserInsureSuccess"));
	//判断在线
	tagBindParameter * pBindParameter=GetBindParameter(LOWORD(dwContextID));
	if ((pBindParameter->dwSocketID!=dwContextID)||(pBindParameter->pIServerUserItem==NULL)) return true;

	//获取用户
	ASSERT(GetBindUserItem(LOWORD(dwContextID))!=NULL);
	IServerUserItem * pIServerUserItem=GetBindUserItem(LOWORD(dwContextID));

	//变量定义
	DBO_GR_UserInsureSuccess * pUserInsureSuccess=(DBO_GR_UserInsureSuccess *)pData;

	//变量定义
	SCORE lFrozenedScore=pUserInsureSuccess->lFrozenedScore;
	SCORE lInsureRevenue=pUserInsureSuccess->lInsureRevenue;
	SCORE lVariationScore=pUserInsureSuccess->lVariationScore;
	SCORE lVariationInsure=pUserInsureSuccess->lVariationInsure;

	//解冻积分
	if ((lFrozenedScore>0L)&&(pIServerUserItem->UnFrozenedUserScore(lFrozenedScore)==false))
	{
		ASSERT(FALSE);
		return false;
	}

	//银行操作
	if (pIServerUserItem->ModifyUserInsure(lVariationScore,lVariationInsure,lInsureRevenue)==false)
	{
		ASSERT(FALSE);
		return false;
	}

	//变量定义
	CMD_GR_S_UserInsureSuccess UserInsureSuccess;
	ZeroMemory(&UserInsureSuccess,sizeof(UserInsureSuccess));

	//构造变量
	UserInsureSuccess.cbActivityGame=pUserInsureSuccess->cbActivityGame;
	UserInsureSuccess.lUserScore=pIServerUserItem->GetUserScore()+pIServerUserItem->GetTrusteeScore();
	UserInsureSuccess.lUserInsure=pUserInsureSuccess->lSourceInsure+pUserInsureSuccess->lVariationInsure;
	lstrcpyn(UserInsureSuccess.szDescribeString,pUserInsureSuccess->szDescribeString,CountArray(UserInsureSuccess.szDescribeString));

	//发送数据
	WORD wDescribe=CountStringBuffer(UserInsureSuccess.szDescribeString);
	WORD wHeadSize=sizeof(UserInsureSuccess)-sizeof(UserInsureSuccess.szDescribeString);
	SendData(pIServerUserItem,MDM_GR_INSURE,SUB_GR_USER_INSURE_SUCCESS,&UserInsureSuccess,wHeadSize+wDescribe);

//	LOGI(_T("CAttemperEngineSink::OnDBUserInsureSuccess end"));
	return true;
}

//操作失败
bool CAttemperEngineSink::OnDBUserInsureFailure(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//判断在线
	tagBindParameter * pBindParameter=GetBindParameter(LOWORD(dwContextID));
	if ((pBindParameter->dwSocketID!=dwContextID)||(pBindParameter->pIServerUserItem==NULL)) return true;

	//获取用户
	WORD wBindIndex=LOWORD(dwContextID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//发送错误
	DBO_GR_UserInsureFailure * pUserInsureFailure=(DBO_GR_UserInsureFailure *)pData;
	SendInsureFailure(pIServerUserItem,pUserInsureFailure->szDescribeString,pUserInsureFailure->lResultCode,pUserInsureFailure->cbActivityGame);

	//解冻积分
	if ((pUserInsureFailure->lFrozenedScore>0L)&&(pIServerUserItem->UnFrozenedUserScore(pUserInsureFailure->lFrozenedScore)==false))
	{
		ASSERT(FALSE);
		return false;
	}

	return true;
}

//用户信息
bool CAttemperEngineSink::OnDBUserInsureUserInfo(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//判断在线
	tagBindParameter * pBindParameter=GetBindParameter(LOWORD(dwContextID));
	if ((pBindParameter->dwSocketID!=dwContextID)||(pBindParameter->pIServerUserItem==NULL)) return true;

	//获取用户
	ASSERT(GetBindUserItem(LOWORD(dwContextID))!=NULL);
	IServerUserItem * pIServerUserItem=GetBindUserItem(LOWORD(dwContextID));

	//变量定义
	DBO_GR_UserTransferUserInfo * pTransferUserInfo=(DBO_GR_UserTransferUserInfo *)pData;

	//变量定义
	CMD_GR_S_UserTransferUserInfo UserTransferUserInfo;
	ZeroMemory(&UserTransferUserInfo,sizeof(UserTransferUserInfo));

	//构造变量
	UserTransferUserInfo.cbActivityGame=pTransferUserInfo->cbActivityGame;
	UserTransferUserInfo.dwTargetGameID=pTransferUserInfo->dwGameID;
	lstrcpyn(UserTransferUserInfo.szNickName,pTransferUserInfo->szNickName,CountArray(UserTransferUserInfo.szNickName));

	//发送数据
	SendData(pIServerUserItem,MDM_GR_INSURE,SUB_GR_USER_TRANSFER_USER_INFO,&UserTransferUserInfo,sizeof(UserTransferUserInfo));

	return true;
}

//道具成功
bool CAttemperEngineSink::OnDBPropertySuccess(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//变量定义
	DBO_GR_S_PropertySuccess * pPropertySuccess=(DBO_GR_S_PropertySuccess *)pData;

	//获取用户
	IServerUserItem * pISourceUserItem=m_ServerUserManager.SearchUserItem(pPropertySuccess->dwSourceUserID);
	IServerUserItem * pITargetUserItem=m_ServerUserManager.SearchUserItem(pPropertySuccess->dwTargetUserID);

	//赠送用户
	if (pISourceUserItem!=NULL)
	{
		//变量定义
		SCORE lFrozenedScore=pPropertySuccess->lFrozenedScore;

		//解冻积分
		if ((lFrozenedScore>0L)&&(pISourceUserItem->UnFrozenedUserScore(lFrozenedScore)==false))
		{
			ASSERT(FALSE);
			return false;
		}
	}

	//更新魅力
	if (pISourceUserItem!=NULL)
	{
		pISourceUserItem->ModifyUserProperty(0,pPropertySuccess->lSendLoveLiness);
	}

	//更新魅力
	if(pITargetUserItem!=NULL)
	{
		pITargetUserItem->ModifyUserProperty(0,pPropertySuccess->lRecvLoveLiness);
	}

	//变量定义
	DWORD dwCurrentTime=(DWORD)time(NULL);
	tagUserProperty * pUserProperty = pITargetUserItem->GetUserProperty();

	//道具处理	
	switch(pPropertySuccess->wPropertyIndex)
	{
	case PROPERTY_ID_TWO_CARD:       //双倍积分
		{
			//使用判断
			if((pUserProperty->wPropertyUseMark&PT_USE_MARK_DOUBLE_SCORE)!=0)
			{
				//变量定义
				DWORD  dwValidTime=pUserProperty->PropertyInfo[0].wPropertyCount*pUserProperty->PropertyInfo[0].dwValidNum;
				if(pUserProperty->PropertyInfo[0].dwEffectTime+dwValidTime<dwCurrentTime)
				{
					pUserProperty->PropertyInfo[0].dwEffectTime=dwCurrentTime;
					pUserProperty->PropertyInfo[0].wPropertyCount=pPropertySuccess->wItemCount;
					pUserProperty->PropertyInfo[0].dwValidNum=VALID_TIME_DOUBLE_SCORE;
				}
				else
				{
					//数目累加
					pUserProperty->PropertyInfo[0].wPropertyCount+=pPropertySuccess->wItemCount;
				}
			}
			else
			{
				//设置信息
				pUserProperty->PropertyInfo[0].dwEffectTime=dwCurrentTime;
				pUserProperty->PropertyInfo[0].wPropertyCount=pPropertySuccess->wItemCount;
				pUserProperty->PropertyInfo[0].dwValidNum=VALID_TIME_DOUBLE_SCORE;
				pUserProperty->wPropertyUseMark |= PT_USE_MARK_DOUBLE_SCORE;
			}

			break;
		}
	case PROPERTY_ID_FOUR_CARD:      //四倍积分
		{
			//使用判断
			if((pUserProperty->wPropertyUseMark&PT_USE_MARK_FOURE_SCORE)!=0)
			{
				//变量定义
				DWORD  dwValidTime=pUserProperty->PropertyInfo[1].wPropertyCount*pUserProperty->PropertyInfo[1].dwValidNum;
				if(pUserProperty->PropertyInfo[1].dwEffectTime+dwValidTime<dwCurrentTime)
				{
					pUserProperty->PropertyInfo[1].dwEffectTime=dwCurrentTime;
					pUserProperty->PropertyInfo[1].wPropertyCount=pPropertySuccess->wItemCount;
					pUserProperty->PropertyInfo[1].dwValidNum=VALID_TIME_FOUR_SCORE;
				}
				else
				{
					//数目累加
					pUserProperty->PropertyInfo[1].wPropertyCount+=pPropertySuccess->wItemCount;
				}
			}
			else
			{
				//设置信息
				pUserProperty->PropertyInfo[1].dwEffectTime=dwCurrentTime;
				pUserProperty->PropertyInfo[1].wPropertyCount=pPropertySuccess->wItemCount;
				pUserProperty->PropertyInfo[1].dwValidNum=VALID_TIME_FOUR_SCORE;
				pUserProperty->wPropertyUseMark |= PT_USE_MARK_FOURE_SCORE;
			}
			break;
		}
	case PROPERTY_ID_SCORE_CLEAR:    //负分清零
		{
			//变量定义
			SCORE lCurrScore = pITargetUserItem->GetUserScore();
			if ( lCurrScore < 0)
			{
				//用户信息
				tagUserInfo * pUserInfo = pITargetUserItem->GetUserInfo();
				if(pUserInfo==NULL) return true;

				//修改积分
				pUserInfo->lScore=0;
			}
			break;
		}
	case PROPERTY_ID_ESCAPE_CLEAR:   //逃跑清零
		{
			//用户信息
			tagUserInfo * pUserInfo = pITargetUserItem->GetUserInfo();
			if(pUserInfo==NULL) return true;

			//修改逃跑率
			if(pUserInfo->dwFleeCount > 0)
			{
				pUserInfo->dwFleeCount=0;
			}

			break;
		}
	case PROPERTY_ID_GUARDKICK_CARD: //防踢卡
		{
			//使用判断
			if((pUserProperty->wPropertyUseMark&PT_USE_MARK_GUARDKICK_CARD)!=0)
			{
				//变量定义
				DWORD  dwValidTime=pUserProperty->PropertyInfo[2].wPropertyCount*pUserProperty->PropertyInfo[2].dwValidNum;
				if(pUserProperty->PropertyInfo[2].dwEffectTime+dwValidTime<dwCurrentTime)
				{
					pUserProperty->PropertyInfo[2].dwEffectTime=dwCurrentTime;
					pUserProperty->PropertyInfo[2].wPropertyCount=pPropertySuccess->wItemCount;
					pUserProperty->PropertyInfo[2].dwValidNum=VALID_TIME_GUARDKICK_CARD;
				}
				else
				{
					//数目累加
					pUserProperty->PropertyInfo[2].wPropertyCount+=pPropertySuccess->wItemCount;
				}
			}
			else
			{
				//设置信息
				pUserProperty->PropertyInfo[2].dwEffectTime=dwCurrentTime;
				pUserProperty->PropertyInfo[2].wPropertyCount=pPropertySuccess->wItemCount;
				pUserProperty->PropertyInfo[2].dwValidNum=VALID_TIME_GUARDKICK_CARD;
				pUserProperty->wPropertyUseMark |= PT_USE_MARK_GUARDKICK_CARD;
			}

			break;
		}
	case PROPERTY_ID_POSSESS:        //附身符
		{
			//使用判断
			if((pUserProperty->wPropertyUseMark&PT_USE_MARK_POSSESS)!=0)
			{
				//变量定义
				DWORD  dwValidTime=pUserProperty->PropertyInfo[3].wPropertyCount*pUserProperty->PropertyInfo[3].dwValidNum;
				if(pUserProperty->PropertyInfo[3].dwEffectTime+dwValidTime<dwCurrentTime)
				{
					pUserProperty->PropertyInfo[3].dwEffectTime=dwCurrentTime;
					pUserProperty->PropertyInfo[3].wPropertyCount=pPropertySuccess->wItemCount;
					pUserProperty->PropertyInfo[3].dwValidNum=VALID_TIME_POSSESS;
				}
				else
				{
					//数目累加
					pUserProperty->PropertyInfo[3].wPropertyCount+=pPropertySuccess->wItemCount;
				}
			}
			else
			{
				//设置信息
				pUserProperty->PropertyInfo[3].dwEffectTime=dwCurrentTime;
				pUserProperty->PropertyInfo[3].wPropertyCount=pPropertySuccess->wItemCount;
				pUserProperty->PropertyInfo[3].dwValidNum=VALID_TIME_POSSESS;
				pUserProperty->wPropertyUseMark |= PT_USE_MARK_POSSESS;
			}

			break;
		}
	case PROPERTY_ID_BLUERING_CARD:  //蓝钻会员
	case PROPERTY_ID_YELLOWRING_CARD://黄钻会员
	case PROPERTY_ID_WHITERING_CARD: //白钻会员
	case PROPERTY_ID_REDRING_CARD:   //红钻会员
	case PROPERTY_ID_VIPROOM_CARD:   //VIP会员
		{
			//用户信息
			tagUserInfo * pUserInfo = pITargetUserItem->GetUserInfo();
			if(pUserInfo==NULL) return true;

			//更新会员
			pUserInfo->cbMemberOrder=pPropertySuccess->cbMemberOrder;
			
			//修改权限
			pITargetUserItem->ModifyUserRight(pPropertySuccess->dwUserRight,0);

			//发送消息
			SendPropertyEffect(pITargetUserItem);

			break;
		}
	default:  // 全部礼物
		{			
			break;
		}
	}

	//消费方式
	if(pPropertySuccess->cbConsumeScore==FALSE)
	{
		pISourceUserItem->ModifyUserInsure(0,-pPropertySuccess->lConsumeGold,0);
	}

	//喇叭判断
	if(pPropertySuccess->wPropertyIndex==PROPERTY_ID_TRUMPET|| pPropertySuccess->wPropertyIndex==PROPERTY_ID_TYPHON)
		return true;

	//变量定义
	CMD_GR_S_PropertySuccess PropertySuccess;
	ZeroMemory(&PropertySuccess,sizeof(PropertySuccess));

	//设置变量
	PropertySuccess.cbRequestArea=pPropertySuccess->cbRequestArea;
	PropertySuccess.wItemCount=pPropertySuccess->wItemCount;
	PropertySuccess.wPropertyIndex=pPropertySuccess->wPropertyIndex;
	PropertySuccess.dwSourceUserID=pPropertySuccess->dwSourceUserID;
	PropertySuccess.dwTargetUserID=pPropertySuccess->dwTargetUserID;

	//发送消息
	if (pISourceUserItem!=NULL)
	{
		SendData(pISourceUserItem,MDM_GR_USER,SUB_GR_PROPERTY_SUCCESS,&PropertySuccess,sizeof(PropertySuccess));
	}

	//发送消息
	if (pITargetUserItem!=NULL && pITargetUserItem!=pISourceUserItem)
	{
		SendData(pITargetUserItem,MDM_GR_USER,SUB_GR_PROPERTY_SUCCESS,&PropertySuccess,sizeof(PropertySuccess));
	}

	//广播礼物
	if(GetPropertyType(pPropertySuccess->wPropertyIndex)==PT_TYPE_PRESENT)
		SendPropertyMessage(PropertySuccess.dwSourceUserID,PropertySuccess.dwTargetUserID,PropertySuccess.wPropertyIndex,
		PropertySuccess.wItemCount);

	return true;
}

//道具失败
bool CAttemperEngineSink::OnDBPropertyFailure(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//参数校验
	ASSERT(wDataSize==sizeof(DBO_GR_PropertyFailure));
	if(wDataSize!=sizeof(DBO_GR_PropertyFailure)) return false;

	//提取数据
	DBO_GR_PropertyFailure * pPropertyFailure = (DBO_GR_PropertyFailure *)pData;
	if(pPropertyFailure==NULL) return false;

	//变量定义
	WORD wBindIndex=LOWORD(dwContextID);
	tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);
	if(pBindParameter==NULL) return false;
	if(pBindParameter->pIServerUserItem==NULL) return false;

	//发送消息
	return SendPropertyFailure(pBindParameter->pIServerUserItem,pPropertyFailure->szDescribeString,0L,pPropertyFailure->cbRequestArea);
}

//系统消息
bool CAttemperEngineSink::OnDBSystemMessage(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//参数校验
	ASSERT(wDataSize==sizeof(DBR_GR_SystemMessage));
	if(wDataSize!=sizeof(DBR_GR_SystemMessage)) return false;

	//提取数据
	DBR_GR_SystemMessage * pSystemMessage = (DBR_GR_SystemMessage *)pData;
	if(pSystemMessage==NULL) return false;

	//重复判断
	POSITION pos = m_SystemMessageList.GetHeadPosition();
	while(pos != NULL)
	{
		POSITION tempPos = pos;
		tagSystemMessage *pTagSystemMessage = m_SystemMessageList.GetNext(pos);
		if(pTagSystemMessage->SystemMessage.cbMessageID == pSystemMessage->cbMessageID)
		{
			//更新数据
			pTagSystemMessage->dwLastTime=0;
			CopyMemory(&pTagSystemMessage->SystemMessage, pSystemMessage, sizeof(DBR_GR_SystemMessage));

			return true;
		}
	}

	//定于变量
	tagSystemMessage  *pSendMessage=new tagSystemMessage;
	ZeroMemory(pSendMessage, sizeof(tagSystemMessage));

	//设置变量
	CopyMemory(&pSendMessage->SystemMessage,pSystemMessage, sizeof(DBR_GR_SystemMessage));

	//记录消息
	m_SystemMessageList.AddTail(pSendMessage);

	return true;
}

//加载敏感词
bool CAttemperEngineSink::OnDBSensitiveWords(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//开始加载
	if(dwContextID==0xfffe)
	{
		m_WordsFilter.ResetSensitiveWordArray();
		m_pITimerEngine->KillTimer(IDI_LOAD_SENSITIVE_WORD);
		return true;			
	}

	//加载完成
	if(dwContextID==0xffff)
	{
		m_WordsFilter.FinishAdd();
		return true;
	}

	//加载敏感词
	const TCHAR *pWords=(const TCHAR*)pData;
	m_WordsFilter.AddSensitiveWords(pWords);
	return true;
}

bool CAttemperEngineSink::OnDBUserTransferReceipt(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//判断在线
	tagBindParameter * pBindParameter=GetBindParameter(LOWORD(dwContextID));
	if ((pBindParameter->dwSocketID!=dwContextID)||(pBindParameter->pIServerUserItem==NULL)) return true;

	ASSERT(GetBindUserItem(LOWORD(dwContextID))!=NULL);
	IServerUserItem * pIServerUserItem=GetBindUserItem(LOWORD(dwContextID));

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//变量定义
	CMD_GR_UserTransferReceipt UserTransferReceipt;
	ZeroMemory(&UserTransferReceipt,sizeof(UserTransferReceipt));

	//变量定义
	CMD_GR_UserTransferReceipt * pUserTransferReceipt=(CMD_GR_UserTransferReceipt *)pData;

	CopyMemory(&UserTransferReceipt, pUserTransferReceipt, wDataSize);

	LOGI("CAttemperEngineSink::OnDBUserTransferReceipt, dwRecordID="<<pUserTransferReceipt->dwRecordID<<", dwSourceUserID="<<pUserTransferReceipt->dwSourceUserID<<\
		", dwTargetUserID="<<pUserTransferReceipt->dwTargetUserID<<", dwSourceGameID="<<pUserTransferReceipt->dwSourceGameID<<", dwTargetGameID="<<pUserTransferReceipt->dwTargetGameID<<\
		", szSourceNickName="<<pUserTransferReceipt->szSourceNickName<<", szTargetNickName="<<pUserTransferReceipt->szTargetNickName<<\
		", lTransferScore="<<pUserTransferReceipt->lTransferScore<<", lRevenueScore="<<pUserTransferReceipt->lRevenueScore);

	//发送数据
	SendData(pIServerUserItem, MDM_GR_INSURE, SUB_GR_USER_TRANSFER_RECEIPT, &UserTransferReceipt, sizeof(UserTransferReceipt));

	return true;
}

// 操作成功
bool CAttemperEngineSink::OnDBUserOperateSuccess(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//判断在线
	tagBindParameter * pBindParameter=GetBindParameter(LOWORD(dwContextID));
	if ((pBindParameter->dwSocketID!=dwContextID)||(pBindParameter->pIServerUserItem==NULL)) return true;

	ASSERT(GetBindUserItem(LOWORD(dwContextID))!=NULL);
	IServerUserItem * pIServerUserItem=GetBindUserItem(LOWORD(dwContextID));

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//变量定义
	CMD_GP_OperateSuccess OperateSuccess;
	ZeroMemory(&OperateSuccess,sizeof(OperateSuccess));

	//变量定义
	DBO_GR_OperateSuccess * pOperateSuccess=(DBO_GR_OperateSuccess *)pData;

	//构造数据
	OperateSuccess.lResultCode=pOperateSuccess->lResultCode;
	lstrcpyn(OperateSuccess.szDescribeString,pOperateSuccess->szDescribeString,CountArray(OperateSuccess.szDescribeString));

	//发送数据
	WORD wDescribe=CountStringBuffer(OperateSuccess.szDescribeString);
	WORD wHeadSize=sizeof(OperateSuccess)-sizeof(OperateSuccess.szDescribeString);
	
	SendData(pIServerUserItem, MDM_GR_INSURE, SUB_GP_OPERATE_SUCCESS,&OperateSuccess,wHeadSize+wDescribe);

	return true;
}

// 操作失败
bool CAttemperEngineSink::OnDBUserOperateFailure(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//判断在线
	tagBindParameter * pBindParameter=GetBindParameter(LOWORD(dwContextID));
	if ((pBindParameter->dwSocketID!=dwContextID)||(pBindParameter->pIServerUserItem==NULL)) return true;

	ASSERT(GetBindUserItem(LOWORD(dwContextID))!=NULL);
	IServerUserItem * pIServerUserItem=GetBindUserItem(LOWORD(dwContextID));

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;


	//变量定义
	CMD_GP_OperateFailure OperateSuccess;
	ZeroMemory(&OperateSuccess,sizeof(OperateSuccess));

	//变量定义
	DBO_GR_OperateSuccess * pOperateSuccess=(DBO_GR_OperateSuccess *)pData;

	//构造数据
	OperateSuccess.lResultCode=pOperateSuccess->lResultCode;
	lstrcpyn(OperateSuccess.szDescribeString,pOperateSuccess->szDescribeString,CountArray(OperateSuccess.szDescribeString));

	//发送数据
	WORD wDescribe=CountStringBuffer(OperateSuccess.szDescribeString);
	WORD wHeadSize=sizeof(OperateSuccess)-sizeof(OperateSuccess.szDescribeString);
	
	SendData(pIServerUserItem,MDM_GR_INSURE, SUB_GP_OPERATE_SUCCESS,&OperateSuccess,wHeadSize+wDescribe);

	return true;
}

// 加载平衡分曲线
bool CAttemperEngineSink::OnDBBalanceScoreCurve(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//开始加载
	if(dwContextID==0xfffe)
	{
//		LOGI(TEXT("CAttemperEngineSink::OnDBBalanceScoreCurve 开始加载配置"));
		m_BalanceScoreCurve.ResetBalanceScoreCurve();
//		m_pITimerEngine->KillTimer(IDI_LOAD_BALANCE_SCORE_CURVE);
		// 第一次读取配置成功后，再每隔半小时再读一次
//		m_pITimerEngine->SetTimer(IDI_LOAD_BALANCE_SCORE_CURVE,TIME_LOAD_BALANCE_SCORE_CURVE*1000L*6,TIMES_INFINITY,NULL);
		return true;			
	}

	//变量定义
	DBO_GR_BalanceScoreCurve * pBalanceScoreCurve=(DBO_GR_BalanceScoreCurve *)pData;

	// 加载配置
//	LOGI(TEXT("CAttemperEngineSink::OnDBBalanceScoreCurve lScore="<<pBalanceScoreCurve->lScore<<", lBalanceScore="<<pBalanceScoreCurve->lBalanceScore));
	m_BalanceScoreCurve.AddBalanceScoreCurve(pBalanceScoreCurve->lScore, pBalanceScoreCurve->lBalanceScore);

	return true;
}

// 玩家平衡分数据
bool CAttemperEngineSink::OnDBUserBalanceScore(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
//	LOGI(TEXT("CAttemperEngineSink::OnDBUserBalanceScore"));
	//判断在线
	tagBindParameter * pBindParameter=GetBindParameter(LOWORD(dwContextID));
	if (pBindParameter == NULL) return true;
	if ((pBindParameter->dwSocketID!=dwContextID)||(pBindParameter->pIServerUserItem==NULL)) return true;

	//获取用户
	ASSERT(GetBindUserItem(LOWORD(dwContextID))!=NULL);
	IServerUserItem * pIServerUserItem=GetBindUserItem(LOWORD(dwContextID));
	if (pIServerUserItem == NULL)
	{
		return true;
	}

	//变量定义
	DBO_GR_UserBalanceScore * pUserBalanceScore = (DBO_GR_UserBalanceScore *)pData;

// 	pIServerUserItem->SetUserBalanceScoreBuffTime(pUserBalanceScore->lBuffTime);
// 	pIServerUserItem->SetUserBalanceScoreBuffGames(pUserBalanceScore->lBuffGames);
// 	pIServerUserItem->SetUserBalanceScoreBuffAmount(pUserBalanceScore->lBuffAmount);

	// 根据dwBuffTime 或者 dwBuffGames 来判断是否需要增加Buff，调节BalanceScore
//	LONG lBalanceScore = pUserBalanceScore->lBalanceScore;
//	LOGI(TEXT("CAttemperEngineSink::OnDBUserBalanceScore, szNickName="<<pIServerUserItem->GetNickName()<<", lBalanceScore="<<lBalanceScore));

	// 根据总输赢来调节BalanceScore(第一次登陆房间时才需要根据输赢来调节，之后的游戏中的数据， 都是调节过的，直接set即可)
// 	SCORE lTotalWin = 0L;
// 	SCORE lTotalLose = 0L;
// 	if (pUserBalanceScore->bLogon == true)
// 	{
// 		lTotalWin = pIServerUserItem->GetTotalWin();
// 		lTotalLose = pIServerUserItem->GetTotalLose();
// 		lBalanceScore += m_BalanceScoreCurve.CalculateBalanceScore( 0, (lTotalWin+lTotalLose) );
// 	}
// 	pIServerUserItem->SetUserBalanceScore(lBalanceScore);

//	LOGI(TEXT("CAttemperEngineSink::OnDBUserBalanceScore, szNickName="<<pIServerUserItem->GetNickName()<<", lTotalWin="<<lTotalWin<<", lTotalLose="<<lTotalLose<<", BuffTime="<<pUserBalanceScore->lBuffTime<< \
		", BuffGames="<<pUserBalanceScore->lBuffGames<<", BuffAmount="<<pUserBalanceScore->lBuffAmount<<", lBalanceScore="<<lBalanceScore));
	
	return true;
}

//广场赛奖励信息
bool CAttemperEngineSink::OnDBSquareReward(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//变量定义
	DBO_GR_Square_Reward * pSquareReward=(DBO_GR_Square_Reward *)pData;

	CSquareManager::iterator iter=m_SquareManager.find(pSquareReward->wMatchID);
	if(iter!=m_SquareManager.end())
	{
		//广场赛必须已经结束
		if(iter->second.m_bStartSquare)
		{
			LOGE("广场赛<" << iter->second.m_pGameSquareOption->wMatchID <<  "|" << iter->second.m_pGameSquareOption->szSquareName << "> 还没有结束!");
			return true;
		}
		
		iter->second.m_StrSelfRankBonusMap[pSquareReward->dwUserID]=pSquareReward->szAwardInfo;

		if(pSquareReward->cbMemberOrder||pSquareReward->dwUserRight)
		{
			//修改用户会员属性
			IServerUserItem * pITargetUserItem=m_ServerUserManager.SearchUserItem(pSquareReward->dwUserID);

			//用户信息
			tagUserInfo * pUserInfo = pITargetUserItem->GetUserInfo();
			if(pUserInfo==NULL) return true;

			//更新会员
			pUserInfo->cbMemberOrder=pSquareReward->cbMemberOrder;

			//修改权限
			pITargetUserItem->ModifyUserRight(pSquareReward->dwUserRight,0);

			//发送消息
			SendPropertyEffect(pITargetUserItem);
		}

		LOGI("<" << iter->second.m_pGameSquareOption->wMatchID <<  "|" << iter->second.m_pGameSquareOption->szSquareName\
			<< "> CAttemperEngineSink::OnDBSquareReward dwUserID= " << pSquareReward->dwUserID << " wRank= " << pSquareReward->wRank\
			<< " szAwardInfo= " << pSquareReward->szAwardInfo << " dwContextID= " << dwContextID);
		//所有玩家奖励结束，发送奖励描述
		if(dwContextID==0xFFFF)iter->second.SendSquareEnd(20);
	}

	return true;
}

//加载广场赛奖励配置信息
bool CAttemperEngineSink::OnDBLoadSquareReward(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	//变量定义
	DBO_GR_Load_Square_Reward * pLoadSquareReward=(DBO_GR_Load_Square_Reward *)pData;

	CSquareManager::iterator iter=m_SquareManager.find(pLoadSquareReward->wMatchID);
	if(iter!=m_SquareManager.end())
	{
		memset(iter->second.m_pGameSquareOption->RankBonus, 0, sizeof(tagRANKBONUS)*20);
		iter->second.m_pGameSquareOption->cbRankBonus=pLoadSquareReward->cbRankBonus;

		LOGI("<" << iter->second.m_pGameSquareOption->wMatchID <<  "|" << iter->second.m_pGameSquareOption->szSquareName\
			<< "> CAttemperEngineSink::OnDBLoadSquareReward 加载广场赛奖励配置 RankBonusCount= " << iter->second.m_pGameSquareOption->cbRankBonus);

		for(int i=0; i<pLoadSquareReward->cbRankBonus; i++)
		{
			iter->second.m_pGameSquareOption->RankBonus[i].wFirst=pLoadSquareReward->RankBonus[i].wFirst;
			iter->second.m_pGameSquareOption->RankBonus[i].wLast=pLoadSquareReward->RankBonus[i].wLast;
			iter->second.m_pGameSquareOption->RankBonus[i].cbPropertyCount=pLoadSquareReward->RankBonus[i].cbPropertyCount;

			LOGI("<" << iter->second.m_pGameSquareOption->wMatchID <<  "|" << iter->second.m_pGameSquareOption->szSquareName\
				<< "> CAttemperEngineSink::OnDBLoadSquareReward i= " << i << " First= " << iter->second.m_pGameSquareOption->RankBonus[i].wFirst\
				<< " Last= " << iter->second.m_pGameSquareOption->RankBonus[i].wLast\
				<< " PropertyCount= " << iter->second.m_pGameSquareOption->RankBonus[i].cbPropertyCount);

			for(int j=0; j<pLoadSquareReward->RankBonus[i].cbPropertyCount; j++)
			{
				iter->second.m_pGameSquareOption->RankBonus[i].wPropertyID[j]=pLoadSquareReward->RankBonus[i].wPropertyID[j];

				LOGI("<" << iter->second.m_pGameSquareOption->wMatchID <<  "|" << iter->second.m_pGameSquareOption->szSquareName\
					<< "> CAttemperEngineSink::OnDBLoadSquareReward j= " << j << " PropertyID= " << iter->second.m_pGameSquareOption->RankBonus[i].wPropertyID[j]);
			}
		}
	}

	return true;
}

// 自建房间成功
bool CAttemperEngineSink::OnDBCreateCustomTableSuccess(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	DBO_GR_UserCustomTableSuccess * pUserCustomTableSuccess = (DBO_GR_UserCustomTableSuccess *)pData;

	//桌子参数
	tagTableFrameParameter TableFrameParameter;
	ZeroMemory(&TableFrameParameter, sizeof(TableFrameParameter));

	//内核组件
	TableFrameParameter.pITimerEngine = m_pITimerEngine;
	TableFrameParameter.pIKernelDataBaseEngine = m_pIKernelDataBaseEngine;
	TableFrameParameter.pIRecordDataBaseEngine = m_pIRecordDataBaseEngine;

	//服务组件
	TableFrameParameter.pIMainServiceFrame = this;
	TableFrameParameter.pIAndroidUserManager = &m_AndroidUserManager;
	TableFrameParameter.pIGameServiceManager = m_pIGameServiceManager;

	//配置参数
	TableFrameParameter.pGameParameter = m_pGameParameter;
	TableFrameParameter.pGameServiceAttrib = m_pGameServiceAttrib;
	TableFrameParameter.pGameServiceOption = m_pGameServiceOption;

	if (m_pIGameMatchServiceManager != NULL)
		TableFrameParameter.pIGameMatchServiceManager = QUERY_OBJECT_PTR_INTERFACE(m_pIGameMatchServiceManager, IUnknownEx);


	// 判断是否有可重复利用的桌
	// 有的话利用已有的桌子,没有的话说明桌子都已经用完了,给用户反馈警告信息
	CTableFrame* pTableFrame = nullptr;
	if (pUserCustomTableSuccess->wTableID < m_TableFrameArray.size())
	{
		pTableFrame = m_TableFrameArray.at(pUserCustomTableSuccess->wTableID);
	}
	if (pTableFrame == nullptr)
	{
		return true;
	}

	//配置桌子
	// 重置自定义规则
	TableFrameParameter.pTableOption = &(m_pGameServiceTableOption->TableOption[pUserCustomTableSuccess->wTableID]);
	memcpy(TableFrameParameter.pTableOption->cbCustomRule, pUserCustomTableSuccess->cbCustomRule, sizeof(TableFrameParameter.pTableOption->cbCustomRule));
	TableFrameParameter.pTableOption->dwCreatUserID = pUserCustomTableSuccess->dwCreateUserID;
	TableFrameParameter.pTableOption->dwRoundCount = pUserCustomTableSuccess->dwRoundCount;
	TableFrameParameter.pTableOption->TableCreateTime = pUserCustomTableSuccess->tmCreateCustomTable;
	lstrcpyn(TableFrameParameter.pTableOption->szSecret, pUserCustomTableSuccess->szSecret, sizeof(TableFrameParameter.pTableOption->szSecret));
	
	// 重置桌子变量
	if (pTableFrame->InitializationFrame(pUserCustomTableSuccess->wTableID, TableFrameParameter) == false)
	{
		return true;
	}

	// 调整参数
	if (m_pIGameServiceManager)
	{
		m_pIGameServiceManager->RectifyParameter(*m_pGameServiceOption, m_pGameServiceTableOption->TableOption[pUserCustomTableSuccess->wTableID]);
	}

	// 返回成功消息
	CMD_CS_C_UserCustomTableSuccess UserCustomTableSuccess;
	memset(&UserCustomTableSuccess, 0, sizeof(UserCustomTableSuccess));
	UserCustomTableSuccess.wKindID = pUserCustomTableSuccess->wKindID;
	UserCustomTableSuccess.wServerID = pUserCustomTableSuccess->wServerID;
	lstrcpyn(UserCustomTableSuccess.szSecret, pUserCustomTableSuccess->szSecret, CountArray(UserCustomTableSuccess.szSecret));
	UserCustomTableSuccess.dwSocketID = dwContextID;
	UserCustomTableSuccess.dwPropertyCount = pUserCustomTableSuccess->dwPropertyCount;
	m_pITCPSocketService->SendData(MDM_CS_REMOTE_SERVICE, SUB_CS_C_USER_CUSTOM_TABLE_SUCCESS, &UserCustomTableSuccess, sizeof(UserCustomTableSuccess));
	
	return true;
}

// 自建(加入)桌子失败
bool CAttemperEngineSink::OnDBCreateCustomTableFailed(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	DBO_GR_UserCustomTableFailure * pUserCustomTableFailure = (DBO_GR_UserCustomTableFailure *)pData;

	// 解除锁定
	CTableFrame* pTableFrame = nullptr;
	if (pUserCustomTableFailure->wTableID < m_TableFrameArray.size())
	{
		pTableFrame = m_TableFrameArray.at(pUserCustomTableFailure->wTableID);
	}
	if (pTableFrame == nullptr)
	{
		return true;
	}
	// 解除锁定
	pTableFrame->SetLockForCustom(false);

	CMD_CS_C_UserCustomTableFailed UserCustomTableFailed;
	UserCustomTableFailed.lErrorCode = pUserCustomTableFailure->lResultCode;
	lstrcpyn(UserCustomTableFailed.szDescribeString, pUserCustomTableFailure->szDescribeString, CountArray(UserCustomTableFailed.szDescribeString));
	UserCustomTableFailed.dwSocketID = dwContextID;
	m_pITCPSocketService->SendData(MDM_CS_REMOTE_SERVICE, SUB_CS_C_USER_CUSTOM_TABLE_FAILED, &UserCustomTableFailed, sizeof(UserCustomTableFailed));

	return true;
}

// 解散桌子成功
bool CAttemperEngineSink::OnDBDismissCustomTableSuccess(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	DBO_GR_DismissCustomTableSuccess * pDismissCustomTableSuccess = (DBO_GR_DismissCustomTableSuccess *)pData;

	CMD_GF_S_ConludeTable ConludeTable;
	memset(&ConludeTable, 0, sizeof(ConludeTable));
	ConludeTable.wConcludeType = pDismissCustomTableSuccess->wConcludeType;
	ConludeTable.dwTableCreater = pDismissCustomTableSuccess->dwTableCreater;											// 桌子创建者
	ConludeTable.dwDiamondCount = pDismissCustomTableSuccess->dwPropertyCount;
	switch (pDismissCustomTableSuccess->wConcludeType)
	{
		case CONCLUDE_TYPE_NORMAL:				// 1:打满局数,正常结算;
		{
			break;
		}
		case CONCLUDE_TYPE_CREATER_DISMISS:
		{
//			SendGameMessage(TEXT("游戏已被房主解散.游戏未开始,解散房间不会扣除钻石."), SMT_CHAT | SMT_EJECT | SMT_CLOSE_GAME);
			_sntprintf(ConludeTable.szMessage, sizeof(ConludeTable.szMessage), TEXT("游戏已被房主解散.游戏未开始,解散房间不会扣除钻石."));
			break;
		}
		case CONCLUDE_TYPE_TIMEOUT_NOSTART:
		{
			// 提示解散
//			SendGameMessage(TEXT("游戏长时间未开始,已被解散.游戏未开始,解散房间不会扣除钻石."), SMT_CHAT | SMT_EJECT | SMT_CLOSE_GAME);
			_sntprintf(ConludeTable.szMessage, sizeof(ConludeTable.szMessage), TEXT("游戏长时间未开始,已被解散.游戏未开始,解散房间不会扣除钻石."));
			break;
		}
		case CONCLUDE_TYPE_PLAYER_DISMISS:
		{
//			SendGameMessage(TEXT("多数玩家同意解散游戏,游戏已被解散."), SMT_CHAT | SMT_EJECT | SMT_CLOSE_GAME);
			_sntprintf(ConludeTable.szMessage, sizeof(ConludeTable.szMessage), TEXT("多数玩家同意解散游戏,游戏已被解散."));
			break;
		}
		case CONCLUDE_TYPE_TIMEOUT_START:
		{
			_sntprintf(ConludeTable.szMessage, sizeof(ConludeTable.szMessage), TEXT("自建房间长时间未结束,已被解散."));
			break;
		}
		default:
		{
			break;
		}
	}

	// 重置桌子信息
	CTableFrame* pTableFrame = nullptr;
	if (pDismissCustomTableSuccess->wTableID < m_TableFrameArray.size())
	{
		pTableFrame = m_TableFrameArray.at(pDismissCustomTableSuccess->wTableID);
	}
	if (pTableFrame)
	{
		// 玩家站起
		for (WORD i = 0; i < pTableFrame->GetChairCount(); i++)
		{
			IServerUserItem* pTableUserItem = pTableFrame->GetTableUserItem(i);
			if (pTableUserItem == nullptr)
			{
				continue;
			}
			pTableFrame->SendTableData(i, SUB_GF_S_CONLUDETABLE, &ConludeTable, sizeof(ConludeTable), MDM_GF_FRAME);
		}

		// 终止游戏
		if (pTableFrame->IsGameStarted())
		{
			// 游戏开始,终止游戏
			pTableFrame->DismissGame();
		}
		else if (pTableFrame->IsTableStarted())
		{
			// 游戏没开始,但是桌子开始,说明处于两局中间
			// 解散桌子
			switch (pDismissCustomTableSuccess->wConcludeType)
			{
				case CONCLUDE_TYPE_NORMAL:
				case CONCLUDE_TYPE_PLAYER_DISMISS:
				case CONCLUDE_TYPE_TIMEOUT_START:
				{
					pTableFrame->ConcludeTable();
				}
			}
		}

		// 玩家站起
		for (WORD i = 0; i < pTableFrame->GetChairCount(); i++)
		{
			IServerUserItem* pTableUserItem = pTableFrame->GetTableUserItem(i);
			if (pTableUserItem == nullptr)
			{
				continue;
			}
			pTableFrame->PerformStandUpAction(pTableUserItem);
		}

		pTableFrame->Reset();
	}

	return true;
}

// 解散桌子失败
bool CAttemperEngineSink::OnDBDismissCustomTableFailed(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	DBO_GR_DismissCustomTableFailure * pDismissCustomTableFailure = (DBO_GR_DismissCustomTableFailure *)pData;
	LOGE("CAttemperEngineSink::OnDBDismissCustomTableFailed, TableID=" << pDismissCustomTableFailure->wTableID << ", Reason=" << pDismissCustomTableFailure->szDescribeString);
	
	CTableFrame* pTableFrame = nullptr;
	if (pDismissCustomTableFailure->wTableID < m_TableFrameArray.size())
	{
		pTableFrame = m_TableFrameArray.at(pDismissCustomTableFailure->wTableID);
	}
	if (pTableFrame)
	{
		// 解散失败
		pTableFrame->SetLockForDismiss(false);
	}
	return true;
}

// 加入桌子成功
bool CAttemperEngineSink::OnDBJoinCustomTableSuccess(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	LOGI("CAttemperEngineSink::OnDBJoinCustomTableSuccess");
	//获取用户
	WORD wBindIndex = LOWORD(dwContextID);
	IServerUserItem * pIServerUserItem = GetBindUserItem(wBindIndex);
	tagBindParameter * pBindParameter = GetBindParameter(wBindIndex);

	DBO_GR_UserJoinCustomTableSuccess * pUserJoinCustomTableSuccess = (DBO_GR_UserJoinCustomTableSuccess *)pData;
	CTableFrame* pTableFrame = nullptr;
	if (pUserJoinCustomTableSuccess->wTableID < m_TableFrameArray.size())
	{
		pTableFrame = m_TableFrameArray.at(pUserJoinCustomTableSuccess->wTableID);
	}

	if (pTableFrame == nullptr)
	{
		SendRequestFailure(pIServerUserItem, TEXT("您加入的房间不存在,请确认后再加入！"), 0);
		return false;
	}

	// 检查玩家是否已经在座位上了,在的话直接就不用重复坐下
	if (pIServerUserItem->GetTableID() != INVALID_TABLE && pIServerUserItem->GetTableID() != INVALID_CHAIR)
	{
		return true;
	}

	// 检查是否有空座位,可以坐下
	WORD wRequestChairID = INVALID_CHAIR;
	if (pUserJoinCustomTableSuccess->wChairID == INVALID_CHAIR)
	{
		bool bCustomTableCreateIn = false;
		for (WORD i = 0; i < pTableFrame->GetChairCount(); i++)
		{
			IServerUserItem* pIServerUserItem = pTableFrame->GetTableUserItem(i);
			if (pIServerUserItem == nullptr)
			{
				continue;
			}
			if (pIServerUserItem->GetUserID() == pTableFrame->GetGameTableOption()->dwCreatUserID)
			{
				// 开房者已经在座位上
				bCustomTableCreateIn = true;
				break;
			}
		}

		// 检查是否有空位置
		if (pTableFrame->GetNullChairCount() == 1 && bCustomTableCreateIn == false && pIServerUserItem->GetUserID() != pTableFrame->GetGameTableOption()->dwCreatUserID)
		{
			// 只剩一个座位,并且开房者没在座位上
			LOGW("CAttemperEngineSink::OnTCPNetworkMainUserCustom SUB_GR_USER_JOIN_TABLE 只剩一个位置,且开房者没在座位上");
			SendRequestFailure(pIServerUserItem, TEXT("由于此游戏桌暂时没有可以让您加入的位置了，请选择另外的游戏桌！"), 0);
			return true;
		}
		else
		{
			// 没有入座过,随机安排座位
			wRequestChairID = pTableFrame->GetRandNullChairID();
		}
	}
	else
	{
		wRequestChairID = pUserJoinCustomTableSuccess->wChairID;
	}

	//结果判断
	if (wRequestChairID == INVALID_CHAIR)
	{
		LOGW("CAttemperEngineSink::OnTCPNetworkMainUserCustom SUB_GR_USER_JOIN_TABLE 桌子没有空位置了");
		SendRequestFailure(pIServerUserItem, TEXT("由于此游戏桌暂时没有可以让您加入的位置了，请选择另外的游戏桌！"), 0);
		return true;
	}

	// 发送同桌信息
	for (WORD j = 0; j < pTableFrame->GetChairCount(); j++)
	{
		IServerUserItem* pTableUserItem = pTableFrame->GetTableUserItem(j);
		if (pTableUserItem == nullptr || pTableUserItem == pIServerUserItem)
		{
			continue;
		}

		// 同桌的信息发送给自己
		SendUserInfoPacket(pTableUserItem, dwContextID);

		// 自己的信息发送给同桌
		tagBindParameter * pBindParameter = GetBindParameter(pTableUserItem->GetBindIndex());
		if (pBindParameter != nullptr)
		{
			SendUserInfoPacket(pIServerUserItem, pBindParameter->dwSocketID);
		}
	}

	// 安排坐下
	if ( pTableFrame->PerformSitDownAction(wRequestChairID, pIServerUserItem) == false )
	{
//		SendRequestFailure(pIServerUserItem, TEXT("位置错误,进入房间失败！"), 0);
		return true;
	}
	else
	{
		// 坐下成功
	}
	return true;
}

// 加入桌子失败
bool CAttemperEngineSink::OnDBJoinCustomTableFailed(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	DBO_GR_UserJoinCustomTableFailure* pUserRoundScoreInCustomTable = (DBO_GR_UserJoinCustomTableFailure*)pData;

	WORD wBindIndex = LOWORD(dwContextID);
	IServerUserItem * pIServerUserItem = GetBindUserItem(wBindIndex);
	SendRequestFailure(pIServerUserItem, pUserRoundScoreInCustomTable->szDescribeString, 0);

	return true;
}

// 玩家在自定义桌子上的分数
bool CAttemperEngineSink::OnDBUserRoundScoreInCustomTable(DWORD dwContextID, VOID * pData, WORD wDataSize)
{
	DBO_GR_UserRoundScoreInCustomTable* pUserRoundScoreInCustomTable = (DBO_GR_UserRoundScoreInCustomTable*)pData;

	if (pUserRoundScoreInCustomTable)
	{
		IServerUserItem* pServerUserItem = m_ServerUserManager.SearchUserItem(pUserRoundScoreInCustomTable->dwUserID);
		if (pServerUserItem)
		{
			tagVariationInfo RecordInfo;
			pServerUserItem->QueryRecordInfo(RecordInfo);
			RecordInfo.lScore += pUserRoundScoreInCustomTable->lRoundScore;
			pServerUserItem->SetRecordInfo(RecordInfo);
		}
	}
	return true;
}

//注册事件
bool CAttemperEngineSink::OnTCPSocketMainRegister(WORD wSubCmdID, VOID * pData, WORD wDataSize)
{
	switch (wSubCmdID)
	{
	case SUB_CS_S_REGISTER_FAILURE:		//注册失败
		{
			//变量定义
			CMD_CS_S_RegisterFailure * pRegisterFailure=(CMD_CS_S_RegisterFailure *)pData;

			//效验参数
			ASSERT(wDataSize>=(sizeof(CMD_CS_S_RegisterFailure)-sizeof(pRegisterFailure->szDescribeString)));
			if (wDataSize<(sizeof(CMD_CS_S_RegisterFailure)-sizeof(pRegisterFailure->szDescribeString))) return false;

			//关闭处理
			m_bNeekCorrespond=false;
			m_pITCPSocketService->CloseSocket();

			//显示消息
			if (lstrlen(pRegisterFailure->szDescribeString)>0)
			{
				CTraceService::TraceString(pRegisterFailure->szDescribeString,TraceLevel_Exception);
				LOGI("CAttemperEngineSink::OnTCPSocketMainRegister, "<<pRegisterFailure->szDescribeString);
			}

			//事件通知
			CP_ControlResult ControlResult;
			ControlResult.cbSuccess=ER_FAILURE;
			SendUIControlPacket(UI_CORRESPOND_RESULT,&ControlResult,sizeof(ControlResult));

			return true;
		}
	}

	return true;
}

//列表事件
bool CAttemperEngineSink::OnTCPSocketMainServiceInfo(WORD wSubCmdID, VOID * pData, WORD wDataSize)
{
	switch (wSubCmdID)
	{
	case SUB_CS_S_SERVER_INFO:		//房间信息
		{
			//废弃列表
			m_ServerListManager.DisuseServerItem();

			return true;
		}
	case SUB_CS_S_SERVER_ONLINE:	//房间人数
		{
			//效验参数
			ASSERT(wDataSize==sizeof(CMD_CS_S_ServerOnLine));
			if (wDataSize!=sizeof(CMD_CS_S_ServerOnLine)) return false;

			//变量定义
			CMD_CS_S_ServerOnLine * pServerOnLine=(CMD_CS_S_ServerOnLine *)pData;

			//查找房间
			CGameServerItem * pGameServerItem=m_ServerListManager.SearchGameServer(pServerOnLine->wServerID);

			//设置人数
			if (pGameServerItem!=NULL)
			{
				pGameServerItem->m_GameServer.dwOnLineCount=pServerOnLine->dwOnLineCount;
			}

			return true;
		}
	case SUB_CS_S_SERVER_INSERT:	//房间插入
		{
			//效验参数
			ASSERT(wDataSize%sizeof(tagGameServer)==0);
			if (wDataSize%sizeof(tagGameServer)!=0) return false;

			//变量定义
			WORD wItemCount=wDataSize/sizeof(tagGameServer);
			tagGameServer * pGameServer=(tagGameServer *)pData;

			//更新数据
			for (WORD i=0;i<wItemCount;i++)
			{
				m_ServerListManager.InsertGameServer(pGameServer++);
			}

			return true;
		}
	case SUB_CS_S_SERVER_MODIFY:	//房间修改
		{
			//效验参数
			ASSERT(wDataSize==sizeof(CMD_CS_S_ServerModify));
			if (wDataSize!=sizeof(CMD_CS_S_ServerModify)) return false;

			//变量定义
			CMD_CS_S_ServerModify * pServerModify=(CMD_CS_S_ServerModify *)pData;

			//查找房间
			CGameServerItem * pGameServerItem=m_ServerListManager.SearchGameServer(pServerModify->wServerID);

			//设置房间
			if (pGameServerItem!=NULL)
			{
				pGameServerItem->m_GameServer.wNodeID=pServerModify->wNodeID;
				pGameServerItem->m_GameServer.wSortID=pServerModify->wSortID;
				pGameServerItem->m_GameServer.wServerPort=pServerModify->wServerPort;
				pGameServerItem->m_GameServer.dwOnLineCount=pServerModify->dwOnLineCount;
				pGameServerItem->m_GameServer.dwFullCount=pServerModify->dwFullCount;
				lstrcpyn(pGameServerItem->m_GameServer.szServerName,pServerModify->szServerName,CountArray(pGameServerItem->m_GameServer.szServerName));
				lstrcpyn(pGameServerItem->m_GameServer.szServerAddr,pServerModify->szServerAddr,CountArray(pGameServerItem->m_GameServer.szServerAddr));
			}

			return true;
		}
	case SUB_CS_S_SERVER_REMOVE:	//房间删除
		{
			//效验参数
			ASSERT(wDataSize==sizeof(CMD_CS_S_ServerRemove));
			if (wDataSize!=sizeof(CMD_CS_S_ServerRemove)) return false;

			//变量定义
			CMD_CS_S_ServerRemove * pServerRemove=(CMD_CS_S_ServerRemove *)pData;

			//变量定义
			m_ServerListManager.DeleteGameServer(pServerRemove->wServerID);

			return true;
		}
	case SUB_CS_S_SERVER_FINISH:	//房间完成
		{
			//清理列表
			m_ServerListManager.CleanServerItem();

			//事件处理
			CP_ControlResult ControlResult;
			ControlResult.cbSuccess=ER_SUCCESS;
			SendUIControlPacket(UI_CORRESPOND_RESULT,&ControlResult,sizeof(ControlResult));

			return true;
		}
	}

	return true;
}

//汇总事件
bool CAttemperEngineSink::OnTCPSocketMainUserCollect(WORD wSubCmdID, VOID * pData, WORD wDataSize)
{
	switch (wSubCmdID)
	{
	case SUB_CS_S_COLLECT_REQUEST:	//用户汇总
		{
			//变量定义
			CMD_CS_C_UserEnter UserEnter;
			ZeroMemory(&UserEnter,sizeof(UserEnter));

			//发送用户
			WORD wIndex=0;
			do
			{
				//获取用户
				IServerUserItem * pIServerUserItem=m_ServerUserManager.EnumUserItem(wIndex++);
				if (pIServerUserItem==NULL) break;

				//设置变量
				UserEnter.dwUserID=pIServerUserItem->GetUserID();
				UserEnter.dwGameID=pIServerUserItem->GetGameID();
				lstrcpyn(UserEnter.szNickName,pIServerUserItem->GetNickName(),CountArray(UserEnter.szNickName));

				//辅助信息
				UserEnter.cbGender=pIServerUserItem->GetGender();
				UserEnter.cbMemberOrder=pIServerUserItem->GetMemberOrder();
				UserEnter.cbMasterOrder=pIServerUserItem->GetMasterOrder();

				//发送数据
				ASSERT(m_pITCPSocketService!=NULL);
				m_pITCPSocketService->SendData(MDM_CS_USER_COLLECT,SUB_CS_C_USER_ENTER,&UserEnter,sizeof(UserEnter));

			} while (true);

			//汇报完成
			m_bCollectUser=true;
			m_pITCPSocketService->SendData(MDM_CS_USER_COLLECT,SUB_CS_C_USER_FINISH);

			return true;
		}
	}

	return true;
}

bool CAttemperEngineSink::OnTCPSocketMainRemoteService(WORD wSubCmdID, VOID * pData, WORD wDataSize)
{
	switch (wSubCmdID)
	{
		case SUB_CS_S_USER_CUSTOM_TABLE:	// 创建桌子
		{
			return OnTCPSocketUserCreateCustomTable(pData,wDataSize);
		}
		default:
			break;
	}
	return true;
}

//管理服务
bool CAttemperEngineSink::OnTCPSocketMainManagerService(WORD wSubCmdID, VOID * pData, WORD wDataSize)
{
	switch (wSubCmdID)
	{
	case SUB_CS_S_SYSTEM_MESSAGE:	//系统消息
		{
			//消息处理
			SendSystemMessage((CMD_GR_SendMessage *)pData, wDataSize);

			return true;
		}
	case SUB_CS_S_PROPERTY_TRUMPET:  //喇叭消息
		{
			if (pData == NULL)
			{
				return true;
			}
			// 解析消息,得到发送者的区域ID
			CMD_CS_S_SendTrumpet* pSendTrumpet = (CMD_CS_S_SendTrumpet*)pData;
			CMD_GR_S_SendTrumpet SendTrumpet;
			SendTrumpet.wPropertyIndex = pSendTrumpet->wPropertyIndex;							//道具索引 
			SendTrumpet.dwSendUserID = pSendTrumpet->dwSendUserID;								//用户 I D
			SendTrumpet.TrumpetColor = pSendTrumpet->TrumpetColor;								//喇叭颜色
			_tcsncpy(SendTrumpet.szSendNickName, pSendTrumpet->szSendNickName, sizeof(SendTrumpet.szSendNickName));
			_tcsncpy(SendTrumpet.szTrumpetContent, pSendTrumpet->szTrumpetContent, sizeof(SendTrumpet.szTrumpetContent));
			// 遍历玩家
			DWORD dwAreaID = pSendTrumpet->dwSendUserAreaID;
			WORD wEnumIndex = 0;
			while (wEnumIndex < m_ServerUserManager.GetUserItemCount())
			{
				IServerUserItem *pIUserItem = m_ServerUserManager.EnumUserItem(wEnumIndex++);
				if (pIUserItem == NULL || ( dwAreaID!=0 && pIUserItem->GetAreaID() != dwAreaID))
				{
					continue;
				}
				WORD wBindIndex = pIUserItem->GetBindIndex();
				tagBindParameter * pBindParameter = GetBindParameter(wBindIndex);
				if (pBindParameter)
				{
					m_pITCPNetworkEngine->SendData(pBindParameter->dwSocketID, MDM_GR_USER, SUB_GR_PROPERTY_TRUMPET, &SendTrumpet, sizeof(SendTrumpet));
				}
			}
			//发送数据
			//m_pITCPNetworkEngine->SendDataBatch(MDM_GR_USER,SUB_GR_PROPERTY_TRUMPET,pData,wDataSize,BG_COMPUTER);

			return true;
		}
	}

	return true;
}

//登录处理
bool CAttemperEngineSink::OnTCPNetworkMainLogon(WORD wSubCmdID, VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	switch (wSubCmdID)
	{
	case SUB_GR_LOGON_USERID:		//I D 登录
		{
			return OnTCPNetworkSubLogonUserID(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_LOGON_MOBILE:		//手机登录
		{
			return OnTCPNetworkSubLogonMobile(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_LOGON_ACCOUNTS:		//帐号登录
		{
			return OnTCPNetworkSubLogonAccounts(pData,wDataSize,dwSocketID);
		}
	}

	return true;
}

//用户处理
bool CAttemperEngineSink::OnTCPNetworkMainUser(WORD wSubCmdID, VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	switch (wSubCmdID)
	{
	case SUB_GR_USER_RULE:			//用户规则
		{
			return OnTCPNetworkSubUserRule(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_USER_LOOKON:		//用户旁观
		{
			return OnTCPNetworkSubUserLookon(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_USER_SITDOWN:		//用户坐下
		{
			return OnTCPNetworkSubUserSitDown(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_USER_STANDUP:		//用户起立
		{
			return OnTCPNetworkSubUserStandUp(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_USER_CHAT:			//用户聊天
		{
			return OnTCPNetworkSubUserChat(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_USER_EXPRESSION:	//用户表情
		{
			return OnTCPNetworkSubUserExpression(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_WISPER_CHAT:		//用户私聊
		{
			return OnTCPNetworkSubWisperChat(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_WISPER_EXPRESSION:	//私聊表情
		{
			return OnTCPNetworkSubWisperExpression(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_PROPERTY_BUY:		//购买道具
		{
			return OnTCPNetworkSubPropertyBuy(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_PROPERTY_TRUMPET:   //喇叭道具
		{
			return OnTCPNetwordSubSendTrumpet(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_USER_INVITE_REQ:    //邀请用户
		{
			return OnTCPNetworkSubUserInviteReq(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_USER_REPULSE_SIT:   //拒绝厌友
		{
			return OnTCPNetworkSubUserRepulseSit(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_USER_KICK_USER:    //踢出用户
		{
			return OnTCPNetworkSubMemberKickUser(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_USER_INFO_REQ:     //请求用户信息
		{
			return OnTCPNetworkSubUserInfoReq(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_USER_CHAIR_REQ:    //请求更换位置
		{
			return OnTCPNetworkSubUserChairReq(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_USER_CHAIR_INFO_REQ: //请求椅子用户信息
		{
			return OnTCPNetworkSubChairUserInfoReq(pData,wDataSize,dwSocketID);
		}
	}

	return false;
}

//银行处理
bool CAttemperEngineSink::OnTCPNetworkMainInsure(WORD wSubCmdID, VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
//	LOGI(_T("CAttemperEngineSink::OnTCPNetworkMainInsure, wSubCmdID="<<wSubCmdID));
	switch (wSubCmdID)
	{
	case SUB_GR_QUERY_INSURE_INFO:		//银行查询
		{
			return OnTCPNetworkSubQueryInsureInfo(pData,wDataSize,dwSocketID);
		}
    case SUB_GR_QUERY_TRANSRECORD:
        {
            return OnTCPNetworkSubQueryTransRecord(pData,wDataSize,dwSocketID);
        }
	case SUB_GR_SAVE_SCORE_REQUEST:		//存款请求
		{
			return OnTCPNetworkSubSaveScoreRequest(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_TAKE_SCORE_REQUEST:		//取款请求
		{
			return OnTCPNetworkSubTakeScoreRequest(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_TRANSFER_SCORE_REQUEST:	//转账请求
		{
			return OnTCPNetworkSubTransferScoreRequest(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_QUERY_USER_INFO_REQUEST:	//查询用户
		{
			return OnTCPNetworkSubQueryUserInfoRequest(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_QUERY_NICKNAME_BY_GAMEID:	// 根据GameID查询昵称
		{
			return OnTCPNetworkSubQueryNickNameByGameID(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_VERIFY_INSURE_PASSWORD:		// 验证银行密码
		{
			return OnTCPNetworkSubVerifyInsurePassword(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_MODIFY_INSURE_PASS:			// 更改银行密码
		{
			return OnTCPNetworkSubModifyInsurePassword(pData,wDataSize,dwSocketID);
		}
	}

	return false;
}

//管理处理
bool CAttemperEngineSink::OnTCPNetworkMainManage(WORD wSubCmdID, VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	switch (wSubCmdID)
	{
	case SUB_GR_QUERY_OPTION:		//查询设置
		{
			return OnTCPNetworkSubQueryOption(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_OPTION_SERVER:		//房间设置
		{
			return OnTCPNetworkSubOptionServer(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_KILL_USER:          //踢出用户
		{
			return OnTCPNetworkSubManagerKickUser(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_LIMIT_USER_CHAT:	//限制聊天
		{
			return OnTCPNetworkSubLimitUserChat(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_KICK_ALL_USER:		//踢出用户
		{
			return OnTCPNetworkSubKickAllUser(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_SEND_MESSAGE:		//发布消息
		{
			return OnTCPNetworkSubSendMessage(pData,wDataSize,dwSocketID);
		}
	case SUB_GR_DISMISSGAME:        //解散游戏
		{
			return OnTCPNetworkSubDismissGame(pData,wDataSize,dwSocketID);
		}
	}

	return false;
}

//比赛命令
bool CAttemperEngineSink::OnTCPNetworkMainMatch(WORD wSubCmdID, VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//获取信息
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//效验接口
	ASSERT(m_pIGameMatchServiceManager!=NULL);
	if (m_pIGameMatchServiceManager==NULL) return false;

	//消息处理
	return m_pIGameMatchServiceManager->OnEventSocketMatch(wSubCmdID,pData,wDataSize,pIServerUserItem,dwSocketID);
}

// 用户自建桌子命令
bool CAttemperEngineSink::OnTCPNetworkMainUserCustom(WORD wSubCmdID, VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	switch (wSubCmdID)
	{
	case SUB_GR_USER_JOIN_TABLE:
	{
		return OnTCPNetworkUserJoinCustomTable(pData, wDataSize, dwSocketID);
	}
	default:
		break;
	}

// 	//效验接口
// 	ASSERT(m_pIGameMatchServiceManager != NULL);
// 	if (m_pIGameMatchServiceManager == NULL) return false;
// 
// 	//消息处理
// 	return m_pIGameMatchServiceManager->OnEventSocketUserCustom(wSubCmdID, pData, wDataSize, pIServerUserItem, dwSocketID);
	return true;
}

//游戏处理
bool CAttemperEngineSink::OnTCPNetworkMainGame(WORD wSubCmdID, VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//获取信息
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//处理过虑
	WORD wTableID=pIServerUserItem->GetTableID();
	WORD wChairID=pIServerUserItem->GetChairID();
	if ((wTableID==INVALID_TABLE)||(wChairID==INVALID_CHAIR)) return true;

	//消息处理 
	CTableFrame * pTableFrame=m_TableFrameArray[wTableID];
	return pTableFrame->OnEventSocketGame(wSubCmdID,pData,wDataSize,pIServerUserItem);
}

//框架处理
bool CAttemperEngineSink::OnTCPNetworkMainFrame(WORD wSubCmdID, VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//获取信息
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//处理过虑
	WORD wTableID=pIServerUserItem->GetTableID();
	WORD wChairID=pIServerUserItem->GetChairID();
	if ((wTableID==INVALID_TABLE)||(wChairID==INVALID_CHAIR)) return true;

	//消息处理 
	CTableFrame * pTableFrame=m_TableFrameArray[wTableID];
	return pTableFrame->OnEventSocketFrame(wSubCmdID,pData,wDataSize,pIServerUserItem);
}

//I D 登录
bool CAttemperEngineSink::OnTCPNetworkSubLogonUserID(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize>=sizeof(CMD_GR_LogonUserID));
	if (wDataSize<sizeof(CMD_GR_LogonUserID)) return false;

	//处理消息
	CMD_GR_LogonUserID * pLogonUserID=(CMD_GR_LogonUserID *)pData;
	pLogonUserID->szPassword[CountArray(pLogonUserID->szPassword)-1]=0;
	pLogonUserID->szMachineID[CountArray(pLogonUserID->szMachineID)-1]=0;

	//绑定信息
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIBindUserItem=GetBindUserItem(wBindIndex);
	tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);

	//重复判断
	if ((pBindParameter==NULL)||(pIBindUserItem!=NULL))
	{ 
		ASSERT(FALSE);
		return false;
	}

	//房间判断
	if(pLogonUserID->wKindID != m_pGameServiceOption->wKindID)
	{
		//发送失败
		SendLogonFailure(TEXT("很抱歉，此游戏房间已经关闭了，不允许继续进入！"),LOGON_FAIL_SERVER_INVALIDATION,dwSocketID);
		return true;
	}

	//机器人和真人不许互踢
	IServerUserItem * pIServerUserItem=m_ServerUserManager.SearchUserItem(pLogonUserID->dwUserID);
	if (pIServerUserItem!=NULL)
	{
		if((pIServerUserItem->IsAndroidUser() && (pBindParameter->dwClientAddr!=0L))
			|| (!pIServerUserItem->IsAndroidUser() && (pBindParameter->dwClientAddr==0L)))
		{
			SendRoomMessage(dwSocketID, TEXT("该账号已在此房间游戏，且不允许踢出，请咨询管理员！"), SMT_CHAT|SMT_EJECT|SMT_GLOBAL|SMT_CLOSE_ROOM,(pBindParameter->dwClientAddr==0L));
			return true;
		}
	}

	//版本信息
	pBindParameter->cbClientKind=CLIENT_KIND_COMPUTER;
	pBindParameter->dwPlazaVersion=pLogonUserID->dwPlazaVersion;
	pBindParameter->dwFrameVersion=pLogonUserID->dwFrameVersion;
	pBindParameter->dwProcessVersion=pLogonUserID->dwProcessVersion;

	//大厅版本
	DWORD dwPlazaVersion=pLogonUserID->dwPlazaVersion;
	DWORD dwFrameVersion=pLogonUserID->dwFrameVersion;
	DWORD dwClientVersion=pLogonUserID->dwProcessVersion;
	if (PerformCheckVersion(dwPlazaVersion, dwFrameVersion, dwClientVersion, dwSocketID) == false)
	{
		LOGI("CAttemperEngineSink::OnTCPNetworkSubLogonUserID, PerformCheckVersion==false");
		return true;
	}

	//切换判断
	if((pIServerUserItem!=NULL)&&(pIServerUserItem->ContrastLogonPass(pLogonUserID->szPassword)==true))
	{
		SwitchUserItemConnect(pIServerUserItem,pLogonUserID->szMachineID,wBindIndex);
		return true;
	}

	if(m_pIGameMatchServiceManager!=NULL&&m_pIGameMatchServiceManager->OnEventEnterMatch(dwSocketID,(VOID*)pLogonUserID, pBindParameter->dwClientAddr))
	{
		//SendRoomMessage(dwSocketID, szPrint, SMT_CHAT|SMT_EJECT|SMT_GLOBAL|SMT_CLOSE_ROOM,(pBindParameter->dwClientAddr==0L));
		return true;
	}
	//变量定义
	DBR_GR_LogonUserID LogonUserID;
	ZeroMemory(&LogonUserID,sizeof(LogonUserID));

	//构造数据
	LogonUserID.dwUserID=pLogonUserID->dwUserID;
	LogonUserID.dwClientAddr=pBindParameter->dwClientAddr;
	lstrcpyn(LogonUserID.szPassword,pLogonUserID->szPassword,CountArray(LogonUserID.szPassword));
	lstrcpyn(LogonUserID.szMachineID,pLogonUserID->szMachineID,CountArray(LogonUserID.szMachineID));

	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(LogonUserID.dwUserID,DBR_GR_LOGON_USERID,dwSocketID,&LogonUserID,sizeof(LogonUserID));

#ifdef _DEBUG
	HANDLE handle = GetCurrentProcess();
	PROCESS_MEMORY_COUNTERS pmc;
	GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
	LOGD("WorkingSetSize = " << (pmc.WorkingSetSize / 1000));
#endif

	return true;
}

//手机登录
bool CAttemperEngineSink::OnTCPNetworkSubLogonMobile(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize>=sizeof(CMD_GR_LogonMobile));
	if (wDataSize<sizeof(CMD_GR_LogonMobile)) return false;

	//处理消息
	CMD_GR_LogonMobile * pLogonMobile=(CMD_GR_LogonMobile *)pData;
	pLogonMobile->szPassword[CountArray(pLogonMobile->szPassword)-1]=0;
	pLogonMobile->szMachineID[CountArray(pLogonMobile->szMachineID)-1]=0;

	//绑定信息
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIBindUserItem=GetBindUserItem(wBindIndex);
	tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);

	//重复判断
	if ((pBindParameter==NULL)||(pIBindUserItem!=NULL))
	{ 
		ASSERT(FALSE);
		return false;
	}

	//机器人和真人不许互踢
	IServerUserItem * pIServerUserItem=m_ServerUserManager.SearchUserItem(pLogonMobile->dwUserID);
	if (pIServerUserItem!=NULL)
	{
		if((pIServerUserItem->IsAndroidUser() && (pBindParameter->dwClientAddr!=0L))
			|| (!pIServerUserItem->IsAndroidUser() && (pBindParameter->dwClientAddr==0L)))
		{
			SendRoomMessage(dwSocketID, TEXT("该账号已在此房间游戏，且不允许踢出，请咨询管理员！"), SMT_CHAT|SMT_EJECT|SMT_GLOBAL|SMT_CLOSE_ROOM,(pBindParameter->dwClientAddr==0L));
			return false;
		}
	}

	//版本信息
	pBindParameter->cbClientKind=CLIENT_KIND_MOBILE;
	pBindParameter->dwProcessVersion=pLogonMobile->dwProcessVersion;

	//大厅版本(暂时不去检查版本)
//	DWORD dwClientVersion=pLogonMobile->dwProcessVersion;
//	if (PerformCheckVersion(0L,0L,dwClientVersion,dwSocketID, true)==false) return true;

	//切换判断
	if ((pIServerUserItem!=NULL)&&(pIServerUserItem->ContrastLogonPass(pLogonMobile->szPassword)==true))
	{
		SwitchUserItemConnect(pIServerUserItem,pLogonMobile->szMachineID,wBindIndex,pLogonMobile->cbDeviceType,pLogonMobile->wBehaviorFlags,pLogonMobile->wPageTableCount);
		return true;
	}

	//变量定义
	DBR_GR_LogonMobile LogonMobile;
	ZeroMemory(&LogonMobile,sizeof(LogonMobile));

	//构造数据
	LogonMobile.dwUserID=pLogonMobile->dwUserID;
	LogonMobile.dwClientAddr=pBindParameter->dwClientAddr;
	lstrcpyn(LogonMobile.szPassword,pLogonMobile->szPassword,CountArray(LogonMobile.szPassword));
	lstrcpyn(LogonMobile.szMachineID,pLogonMobile->szMachineID,CountArray(LogonMobile.szMachineID));
	LogonMobile.cbDeviceType=pLogonMobile->cbDeviceType;
	LogonMobile.wBehaviorFlags=pLogonMobile->wBehaviorFlags;
	LogonMobile.wPageTableCount=pLogonMobile->wPageTableCount;

	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(LogonMobile.dwUserID,DBR_GR_LOGON_MOBILE,dwSocketID,&LogonMobile,sizeof(LogonMobile));

	return true;
}

//帐号登录
bool CAttemperEngineSink::OnTCPNetworkSubLogonAccounts(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize>=sizeof(CMD_GR_LogonAccounts));
	if (wDataSize<=sizeof(CMD_GR_LogonAccounts)) return false;

	//处理消息
	CMD_GR_LogonAccounts * pLogonAccounts=(CMD_GR_LogonAccounts *)pData;
	pLogonAccounts->szPassword[CountArray(pLogonAccounts->szPassword)-1]=0;
	pLogonAccounts->szAccounts[CountArray(pLogonAccounts->szAccounts)-1]=0;
	pLogonAccounts->szMachineID[CountArray(pLogonAccounts->szMachineID)-1]=0;

	//绑定信息
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIBindUserItem=GetBindUserItem(wBindIndex);
	tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);

	//重复判断
	if (pIBindUserItem!=NULL)
	{ 
		ASSERT(FALSE);
		return false;
	}

	//机器人和真人不许互踢
	IServerUserItem * pIServerUserItem=m_ServerUserManager.SearchUserItem(pLogonAccounts->szAccounts);
	if (pIServerUserItem!=NULL)
	{
		if((pIServerUserItem->IsAndroidUser() && (pBindParameter->dwClientAddr!=0L))
			|| (!pIServerUserItem->IsAndroidUser() && (pBindParameter->dwClientAddr==0L)))
		{
			SendRoomMessage(dwSocketID, TEXT("该账号已在此房间游戏，且不允许踢出，请咨询管理员！"), SMT_CHAT|SMT_EJECT|SMT_GLOBAL|SMT_CLOSE_ROOM,(pBindParameter->dwClientAddr==0L));
			return false;
		}
	}

	//版本信息
	pBindParameter->cbClientKind=CLIENT_KIND_COMPUTER;
	pBindParameter->dwPlazaVersion=pLogonAccounts->dwPlazaVersion;
	pBindParameter->dwFrameVersion=pLogonAccounts->dwFrameVersion;
	pBindParameter->dwProcessVersion=pLogonAccounts->dwProcessVersion;

	//大厅版本
	DWORD dwPlazaVersion=pLogonAccounts->dwPlazaVersion;
	DWORD dwFrameVersion=pLogonAccounts->dwFrameVersion;
	DWORD dwClientVersion=pLogonAccounts->dwProcessVersion;
	if (PerformCheckVersion(dwPlazaVersion, dwFrameVersion, dwClientVersion, dwSocketID) == false)
	{
		LOGI("CAttemperEngineSink::OnTCPNetworkSubLogonAccounts, PerformCheckVersion==false");
		return true;
	}

	//切换判断
	if ((pIServerUserItem!=NULL)&&(pIServerUserItem->ContrastLogonPass(pLogonAccounts->szPassword)==true))
	{
		SwitchUserItemConnect(pIServerUserItem,pLogonAccounts->szMachineID,wBindIndex);
		return true;
	}

	//变量定义
	DBR_GR_LogonAccounts LogonAccounts;
	ZeroMemory(&LogonAccounts,sizeof(LogonAccounts));

	//构造数据
	LogonAccounts.dwClientAddr=pBindParameter->dwClientAddr;
	lstrcpyn(LogonAccounts.szAccounts,pLogonAccounts->szAccounts,CountArray(LogonAccounts.szAccounts));
	lstrcpyn(LogonAccounts.szPassword,pLogonAccounts->szPassword,CountArray(LogonAccounts.szPassword));
	lstrcpyn(LogonAccounts.szMachineID,pLogonAccounts->szMachineID,CountArray(LogonAccounts.szMachineID));

	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(0L,DBR_GR_LOGON_ACCOUNTS,dwSocketID,&LogonAccounts,sizeof(LogonAccounts));

	return true;
}

//用户规则
bool CAttemperEngineSink::OnTCPNetworkSubUserRule(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize>=sizeof(CMD_GR_UserRule));
	if (wDataSize<sizeof(CMD_GR_UserRule)) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//规则判断
	//ASSERT(CServerRule::IsForfendGameRule(m_pGameServiceOption->dwServerRule)==false);
	if (CServerRule::IsForfendGameRule(m_pGameServiceOption->dwServerRule)==true) return true;

	//消息处理
	tagUserRule * pUserRule=pIServerUserItem->GetUserRule();
	CMD_GR_UserRule * pCMDUserRule=(CMD_GR_UserRule *)pData;

	//规则标志
	pUserRule->bLimitSameIP=((pCMDUserRule->cbRuleMask&UR_LIMIT_SAME_IP)>0);
	pUserRule->bLimitWinRate=((pCMDUserRule->cbRuleMask&UR_LIMIT_WIN_RATE)>0);
	pUserRule->bLimitFleeRate=((pCMDUserRule->cbRuleMask&UR_LIMIT_FLEE_RATE)>0);
	pUserRule->bLimitGameScore=((pCMDUserRule->cbRuleMask&UR_LIMIT_GAME_SCORE)>0);

	//规则属性
	pUserRule->szPassword[0]=0;
	pUserRule->wMinWinRate=pCMDUserRule->wMinWinRate;
	pUserRule->wMaxFleeRate=pCMDUserRule->wMaxFleeRate;
	pUserRule->lMaxGameScore=pCMDUserRule->lMaxGameScore;
	pUserRule->lMinGameScore=pCMDUserRule->lMinGameScore;

	//桌子密码
	if (wDataSize>sizeof(CMD_GR_UserRule))
	{
		//变量定义
		VOID * pDataBuffer=NULL;
		tagDataDescribe DataDescribe;
		CRecvPacketHelper RecvPacket(pCMDUserRule+1,wDataSize-sizeof(CMD_GR_UserRule));

		//提取处理
		while (true)
		{
			//提取数据
			pDataBuffer=RecvPacket.GetData(DataDescribe);
			if (DataDescribe.wDataDescribe==DTP_NULL) break;

			//数据分析
			switch (DataDescribe.wDataDescribe)
			{
			case DTP_GR_TABLE_PASSWORD:		//桌子密码
				{
					//效验数据
					ASSERT(pDataBuffer!=NULL);
					ASSERT(DataDescribe.wDataSize<=sizeof(pUserRule->szPassword));

					//规则判断
					ASSERT(CServerRule::IsForfendLockTable(m_pGameServiceOption->dwServerRule)==false);
					if (CServerRule::IsForfendLockTable(m_pGameServiceOption->dwServerRule)==true) break;

					//设置数据
					if (DataDescribe.wDataSize<=sizeof(pUserRule->szPassword))
					{
						CopyMemory(&pUserRule->szPassword,pDataBuffer,DataDescribe.wDataSize);
						pUserRule->szPassword[CountArray(pUserRule->szPassword)-1]=0;
					}

					break;
				}
			}
		}
	}

	return true;
}

//用户旁观
bool CAttemperEngineSink::OnTCPNetworkSubUserLookon(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_UserLookon));
	if (wDataSize!=sizeof(CMD_GR_UserLookon)) return false;

	//效验数据
	CMD_GR_UserLookon * pUserLookon=(CMD_GR_UserLookon *)pData;
	if (pUserLookon->wChairID>=m_pGameServiceAttrib->wChairCount) return false;
	if (pUserLookon->wTableID>=(WORD)m_TableFrameArray.size()) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//消息处理
	WORD wTableID=pIServerUserItem->GetTableID();
	WORD wChairID=pIServerUserItem->GetChairID();
	BYTE cbUserStatus=pIServerUserItem->GetUserStatus();
	if ((wTableID==pUserLookon->wTableID)&&(wChairID==pUserLookon->wChairID)&&(cbUserStatus==US_LOOKON)) return true;

	//用户判断
	if (cbUserStatus==US_TRUSTEE)
	{
		SendRequestFailure(pIServerUserItem,TEXT("您正在托管代打，操作无效，请等待当前游戏结束！"),0);
		return true;
	}
	if (cbUserStatus == US_OFFLINE)
	{
		SendRequestFailure(pIServerUserItem, TEXT("您正处于断线状态, 操作无效, 请等待当前游戏结束！"), 0);
		return true;
	}
	if (cbUserStatus==US_PLAYING)
	{
		SendRequestFailure(pIServerUserItem,TEXT("您正在游戏中，暂时不能离开，请先结束当前游戏！"),0);
		return true;
	}

	//离开处理
	if (wTableID!=INVALID_TABLE)
	{
		CTableFrame * pTableFrame=m_TableFrameArray[wTableID];
		LOGI("CAttemperEngineSink::OnTCPNetworkSubUserLookon PerformStandUpAction, NickName="<<pIServerUserItem->GetNickName());
		if (pTableFrame->PerformStandUpAction(pIServerUserItem)==false) return true;
	}

	//坐下处理
	CTableFrame * pTableFrame=m_TableFrameArray[pUserLookon->wTableID];
	pTableFrame->PerformLookonAction(pUserLookon->wChairID,pIServerUserItem,pUserLookon->szPassword);

	return true;
}

//用户坐下
bool CAttemperEngineSink::OnTCPNetworkSubUserSitDown(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_UserSitDown));
	if (wDataSize!=sizeof(CMD_GR_UserSitDown)) return false;

	//效验数据
	CMD_GR_UserSitDown * pUserSitDown=(CMD_GR_UserSitDown *)pData;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//消息处理
	WORD wTableID=pIServerUserItem->GetTableID();
	WORD wChairID=pIServerUserItem->GetChairID();
	BYTE cbUserStatus=pIServerUserItem->GetUserStatus();

	//重复判断
	if ((pUserSitDown->wTableID<m_pGameServiceOption->wTableCount)&&(pUserSitDown->wChairID<m_pGameServiceAttrib->wChairCount))
	{
		CTableFrame * pTableFrame=m_TableFrameArray[pUserSitDown->wTableID];
		if (pTableFrame->GetTableUserItem(pUserSitDown->wChairID)==pIServerUserItem) return true;
	}

	//用户判断
	if (cbUserStatus==US_TRUSTEE)
	{
		SendRequestFailure(pIServerUserItem,TEXT("您正在托管代打，操作无效，请等待当前游戏结束！"),0);
		return true;
	}
	if (cbUserStatus==US_PLAYING)
	{
		SendRequestFailure(pIServerUserItem,TEXT("您正在游戏中，暂时不能离开，请先结束当前游戏！"),0);
		return true;
	}

	//插入分组
	if ((m_pGameServiceOption->wServerType&GAME_GENRE_MATCH)!=0)
	{
		//返回FALSE为 定时开赛
		if(InsertDistribute(pIServerUserItem))
			return true;
	}

	//离开处理
	if (wTableID!=INVALID_TABLE)
	{
		CTableFrame * pTableFrame=m_TableFrameArray[wTableID];
		LOGI("CAttemperEngineSink::OnTCPNetworkSubUserSitDown PerformStandUpAction, NickName="<<pIServerUserItem->GetNickName());
		if (pTableFrame->PerformStandUpAction(pIServerUserItem)==false) return true;
	}

	//防作弊
	if(CServerRule::IsAllowAvertCheatMode(m_pGameServiceOption->dwServerRule)&&(m_pGameServiceAttrib->wChairCount < MAX_CHAIR))
	{
		if(m_TableFrameArray[0]->EfficacyEnterTableScoreRule(0, pIServerUserItem))
			InsertWaitDistribute(pIServerUserItem);

		return true;
	}

	//请求调整
	WORD wRequestTableID=pUserSitDown->wTableID;
	WORD wRequestChairID=pUserSitDown->wChairID;

	//桌子调整
	if (wRequestTableID>=m_TableFrameArray.size())
	{
		//起始桌子
		WORD wStartTableID=0;
		DWORD dwServerRule=m_pGameServiceOption->dwServerRule;
		if ((CServerRule::IsAllowAvertCheatMode(dwServerRule)==true)&&(m_pGameServiceAttrib->wChairCount<MAX_CHAIR)) wStartTableID=1;

		//动态加入
		bool bDynamicJoin=true;
		if (m_pGameServiceAttrib->cbDynamicJoin==FALSE) bDynamicJoin=false;
		if (CServerRule::IsAllowDynamicJoin(m_pGameServiceOption->dwServerRule)==false) bDynamicJoin=false;

		//寻找位置
		for (size_t i=wStartTableID;i<m_TableFrameArray.size();i++)
		{
			//游戏状态
			if ((m_TableFrameArray[i]->IsGameStarted()==true)&&(bDynamicJoin==false))continue;

			//获取空位
			WORD wNullChairID=m_TableFrameArray[i]->GetNullChairID();

			//调整结果
			if (wNullChairID!=INVALID_CHAIR)
			{
				//设置变量
				wRequestTableID=i;
				wRequestChairID=wNullChairID;

				break;
			}
		}

		//结果判断
		if ((wRequestTableID==INVALID_CHAIR)||(wRequestChairID==INVALID_CHAIR))
		{
			SendRequestFailure(pIServerUserItem,TEXT("当前游戏房间已经人满为患了，暂时没有可以让您加入的位置，请稍后再试！"),0);
			return true;
		}
	}

	//椅子调整
	if (wRequestChairID>=m_pGameServiceAttrib->wChairCount)
	{
		//效验参数
		ASSERT(wRequestTableID<m_TableFrameArray.size());
		if (wRequestTableID>=m_TableFrameArray.size()) return false;

		//查找空位
		wRequestChairID=m_TableFrameArray[wRequestTableID]->GetNullChairID();

		//结果判断
		if (wRequestChairID==INVALID_CHAIR)
		{
			SendRequestFailure(pIServerUserItem,TEXT("由于此游戏桌暂时没有可以让您加入的位置了，请选择另外的游戏桌！"),0);
			return true;
		}
	}

	//坐下处理
	CTableFrame * pTableFrame=m_TableFrameArray[wRequestTableID];
	pTableFrame->PerformSitDownAction(wRequestChairID,pIServerUserItem,pUserSitDown->szPassword);

	return true;
}

//用户起立
bool CAttemperEngineSink::OnTCPNetworkSubUserStandUp(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_UserStandUp));
	if (wDataSize!=sizeof(CMD_GR_UserStandUp)) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//定义变量
	CMD_GR_UserStandUp * pUserStandUp=(CMD_GR_UserStandUp *)pData;

	//取消分组
	if(CServerRule::IsAllowAvertCheatMode(m_pGameServiceOption->dwServerRule)&&(m_pGameServiceAttrib->wChairCount < MAX_CHAIR))
	{
		POSITION pos=m_WaitDistributeList.GetHeadPosition();
		while(pos != NULL)
		{
			POSITION tempPos=pos;
			IServerUserItem *pUserItem = m_WaitDistributeList.GetNext(pos);
			if(pUserItem->GetUserID()==pIServerUserItem->GetUserID())
			{
				m_WaitDistributeList.RemoveAt(tempPos);
				break;
			}
		}

		if(pUserStandUp->wTableID==INVALID_TABLE) return true;
	}

	//效验数据
	if (pUserStandUp->wChairID>=m_pGameServiceAttrib->wChairCount) return false;
	if (pUserStandUp->wTableID>=(WORD)m_TableFrameArray.size()) return false;

	//消息处理
	WORD wTableID=pIServerUserItem->GetTableID();
	WORD wChairID=pIServerUserItem->GetChairID();
	if ((wTableID!=pUserStandUp->wTableID)||(wChairID!=pUserStandUp->wChairID)) return true;

	//用户判断
	if (pIServerUserItem->GetUserStatus()==US_TRUSTEE)
	{
		SendRequestFailure(pIServerUserItem,TEXT("您正在托管代打，操作无效，请等待当前游戏结束！"),0);
		return true;
	}
	if ((pUserStandUp->cbForceLeave==FALSE)&&(pIServerUserItem->GetUserStatus()==US_PLAYING))
	{
		SendRequestFailure(pIServerUserItem,TEXT("您正在游戏中，暂时不能离开，请先结束当前游戏！"),0);
		return true;
	}

	//离开处理
	if (wTableID!=INVALID_TABLE)
	{
		CTableFrame * pTableFrame=m_TableFrameArray[wTableID];
		LOGI("CAttemperEngineSink::OnTCPNetworkSubUserStandUp PerformStandUpAction, NickName="<<pIServerUserItem->GetNickName());
		if (pTableFrame->PerformStandUpAction(pIServerUserItem)==false) return true;
	}

	return true;
}

//用户聊天
bool CAttemperEngineSink::OnTCPNetworkSubUserChat(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//变量定义
	CMD_GR_C_UserChat * pUserChat=(CMD_GR_C_UserChat *)pData;

	//效验参数
	ASSERT(wDataSize<=sizeof(CMD_GR_C_UserChat));
	ASSERT(wDataSize>=(sizeof(CMD_GR_C_UserChat)-sizeof(pUserChat->szChatString)));
	ASSERT(wDataSize==(sizeof(CMD_GR_C_UserChat)-sizeof(pUserChat->szChatString)+pUserChat->wChatLength*sizeof(pUserChat->szChatString[0])));

	//效验参数
	if (wDataSize>sizeof(CMD_GR_C_UserChat)) return false;
	if (wDataSize<(sizeof(CMD_GR_C_UserChat)-sizeof(pUserChat->szChatString))) return false;
	if (wDataSize!=(sizeof(CMD_GR_C_UserChat)-sizeof(pUserChat->szChatString)+pUserChat->wChatLength*sizeof(pUserChat->szChatString[0]))) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pISendUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pISendUserItem!=NULL);
	if (pISendUserItem==NULL) return false;

	//寻找用户
	IServerUserItem * pIRecvUserItem=NULL;
	if (pUserChat->dwTargetUserID!=0)
	{
		pIRecvUserItem=m_ServerUserManager.SearchUserItem(pUserChat->dwTargetUserID);
		if (pIRecvUserItem==NULL) return true;
	}

	//状态判断
	if ((CServerRule::IsForfendRoomChat(m_pGameServiceOption->dwServerRule)==true)&&(pISendUserItem->GetMasterOrder()==0))
	{
		SendRoomMessage(pISendUserItem,TEXT("抱歉，当前此游戏房间禁止用户大厅聊天！"),SMT_CHAT);
		return true;
	}

	//权限判断
	if (CUserRight::CanRoomChat(pISendUserItem->GetUserRight())==false)
	{
		SendRoomMessage(pISendUserItem,TEXT("抱歉，您没有大厅发言的权限，若需要帮助，请联系游戏客服咨询！"),SMT_EJECT|SMT_CHAT);
		return true;
	}

	//构造消息
	CMD_GR_S_UserChat UserChat;
	ZeroMemory(&UserChat,sizeof(UserChat));

	//字符过滤
	SensitiveWordFilter(pUserChat->szChatString,UserChat.szChatString,CountArray(UserChat.szChatString));
	
	//构造数据
	UserChat.dwChatColor=pUserChat->dwChatColor;
	UserChat.wChatLength=pUserChat->wChatLength;
	UserChat.dwSendUserID=pISendUserItem->GetUserID();
	UserChat.dwTargetUserID=pUserChat->dwTargetUserID;
	UserChat.wChatLength=CountStringBuffer(UserChat.szChatString);

	//转发消息
	WORD wHeadSize=sizeof(UserChat)-sizeof(UserChat.szChatString);
	SendData(BG_COMPUTER,MDM_GR_USER,SUB_GR_USER_CHAT,&UserChat,wHeadSize+UserChat.wChatLength*sizeof(UserChat.szChatString[0]));

	return true;
}

//用户表情
bool CAttemperEngineSink::OnTCPNetworkSubUserExpression(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_C_UserExpression));
	if (wDataSize!=sizeof(CMD_GR_C_UserExpression)) return false;

	//变量定义
	CMD_GR_C_UserExpression * pUserExpression=(CMD_GR_C_UserExpression *)pData;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pISendUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pISendUserItem!=NULL);
	if (pISendUserItem==NULL) return false;

	//寻找用户
	IServerUserItem * pIRecvUserItem=NULL;
	if (pUserExpression->dwTargetUserID!=0)
	{
		pIRecvUserItem=m_ServerUserManager.SearchUserItem(pUserExpression->dwTargetUserID);
		if (pIRecvUserItem==NULL) return true;
	}

	//状态判断
	if ((CServerRule::IsForfendRoomChat(m_pGameServiceOption->dwServerRule)==true)&&(pISendUserItem->GetMasterOrder()==0))
	{
		SendRoomMessage(pISendUserItem,TEXT("抱歉，当前此游戏房间禁止用户大厅聊天！"),SMT_CHAT);
		return true;
	}

	//权限判断
	if (CUserRight::CanRoomChat(pISendUserItem->GetUserRight())==false)
	{
		SendRoomMessage(pISendUserItem,TEXT("抱歉，您没有大厅发言的权限，若需要帮助，请联系游戏客服咨询！"),SMT_EJECT|SMT_CHAT);
		return true;
	}

	//构造消息
	CMD_GR_S_UserExpression UserExpression;
	ZeroMemory(&UserExpression,sizeof(UserExpression));

	//构造数据
	UserExpression.wItemIndex=pUserExpression->wItemIndex;
	UserExpression.dwSendUserID=pISendUserItem->GetUserID();
	UserExpression.dwTargetUserID=pUserExpression->dwTargetUserID;

	//转发消息
	SendData(BG_COMPUTER,MDM_GR_USER,SUB_GR_USER_EXPRESSION,&UserExpression,sizeof(UserExpression));

	return true;
}

//用户私聊
bool CAttemperEngineSink::OnTCPNetworkSubWisperChat(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//变量定义
	CMD_GR_C_WisperChat * pWisperChat=(CMD_GR_C_WisperChat *)pData;

	//效验参数
	ASSERT(wDataSize>=(sizeof(CMD_GR_C_WisperChat)-sizeof(pWisperChat->szChatString)));
	ASSERT(wDataSize==(sizeof(CMD_GR_C_WisperChat)-sizeof(pWisperChat->szChatString)+pWisperChat->wChatLength*sizeof(pWisperChat->szChatString[0])));

	//效验参数
	if (wDataSize<(sizeof(CMD_GR_C_WisperChat)-sizeof(pWisperChat->szChatString))) return false;
	if (wDataSize!=(sizeof(CMD_GR_C_WisperChat)-sizeof(pWisperChat->szChatString)+pWisperChat->wChatLength*sizeof(pWisperChat->szChatString[0]))) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pISendUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pISendUserItem!=NULL);
	if (pISendUserItem==NULL) return false;

	//寻找用户
	IServerUserItem * pIRecvUserItem=m_ServerUserManager.SearchUserItem(pWisperChat->dwTargetUserID);
	if (pIRecvUserItem==NULL) return true;

	//状态判断
	if ((CServerRule::IsForfendWisperChat(m_pGameServiceOption->dwServerRule)==true)&&(pISendUserItem->GetMasterOrder()==0))
	{
		SendRoomMessage(pISendUserItem,TEXT("抱歉，当前此游戏房间禁止用户私聊！"),SMT_CHAT);
		return true;
	}

	//同桌判断
	if ((CServerRule::IsForfendWisperOnGame(m_pGameServiceOption->dwServerRule)==true)&&(pISendUserItem->GetMasterOrder()==0))
	{
		//变量定义
		bool bForfend=true;
		WORD wTableIDSend=pISendUserItem->GetTableID();
		WORD wTableIDRecv=pIRecvUserItem->GetTableID();

		//规则判断
		if ((bForfend==true)&&(pIRecvUserItem->GetMasterOrder()!=0)) bForfend=false;
		if ((bForfend==true)&&(pIRecvUserItem->GetMasterOrder()!=0)) bForfend=false;
		if ((bForfend==true)&&(pISendUserItem->GetUserStatus()!=US_PLAYING)) bForfend=false;
		if ((bForfend==true)&&(pIRecvUserItem->GetUserStatus()!=US_PLAYING)) bForfend=false;
		if ((bForfend==true)&&((wTableIDSend==INVALID_TABLE)||(wTableIDSend!=wTableIDRecv))) bForfend=false;

		//提示消息
		if (bForfend==true)
		{
			SendRoomMessage(pISendUserItem,TEXT("抱歉，此游戏房间不允许在游戏中与同桌的玩家私聊！"),SMT_EJECT|SMT_CHAT);
			return true;
		}
	}

	//权限判断
	if (CUserRight::CanWisper(pISendUserItem->GetUserRight())==false)
	{
		SendRoomMessage(pISendUserItem,TEXT("抱歉，您没有发送私聊的权限，若需要帮助，请联系游戏客服咨询！"),SMT_EJECT|SMT_CHAT);
		return true;
	}

	//变量定义
	CMD_GR_S_WisperChat WisperChat;
	ZeroMemory(&WisperChat,sizeof(WisperChat));

	//字符过滤
	SensitiveWordFilter(pWisperChat->szChatString,WisperChat.szChatString,CountArray(WisperChat.szChatString));

	//构造数据
	WisperChat.dwChatColor=pWisperChat->dwChatColor;
	WisperChat.wChatLength=pWisperChat->wChatLength;
	WisperChat.dwSendUserID=pISendUserItem->GetUserID();
	WisperChat.dwTargetUserID=pIRecvUserItem->GetUserID();
	WisperChat.wChatLength=CountStringBuffer(WisperChat.szChatString);

	//转发消息
	WORD wHeadSize=sizeof(WisperChat)-sizeof(WisperChat.szChatString);
	SendData(pISendUserItem,MDM_GR_USER,SUB_GR_WISPER_CHAT,&WisperChat,wHeadSize+WisperChat.wChatLength*sizeof(WisperChat.szChatString[0]));
	SendData(pIRecvUserItem,MDM_GR_USER,SUB_GR_WISPER_CHAT,&WisperChat,wHeadSize+WisperChat.wChatLength*sizeof(WisperChat.szChatString[0]));

	return true;
}

//用户表情
bool CAttemperEngineSink::OnTCPNetworkSubWisperExpression(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_C_WisperExpression));
	if (wDataSize!=sizeof(CMD_GR_C_WisperExpression)) return false;

	//变量定义
	CMD_GR_C_WisperExpression * pWisperExpression=(CMD_GR_C_WisperExpression *)pData;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pISendUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pISendUserItem!=NULL);
	if (pISendUserItem==NULL) return false;

	//寻找用户
	IServerUserItem * pIRecvUserItem=m_ServerUserManager.SearchUserItem(pWisperExpression->dwTargetUserID);
	if (pIRecvUserItem==NULL) return true;

	//状态判断
	if ((CServerRule::IsForfendWisperChat(m_pGameServiceOption->dwServerRule)==true)&&(pISendUserItem->GetMasterOrder()==0))
	{
		SendRoomMessage(pISendUserItem,TEXT("抱歉，当前此游戏房间禁止用户私聊！"),SMT_CHAT);
		return true;
	}

	//同桌判断
	if ((CServerRule::IsForfendWisperOnGame(m_pGameServiceOption->dwServerRule)==true)&&(pISendUserItem->GetMasterOrder()==0))
	{
		//变量定义
		bool bForfend=true;
		WORD wTableIDSend=pISendUserItem->GetTableID();
		WORD wTableIDRecv=pIRecvUserItem->GetTableID();

		//规则判断
		if ((bForfend==true)&&(pIRecvUserItem->GetMasterOrder()!=0)) bForfend=false;
		if ((bForfend==true)&&(pIRecvUserItem->GetMasterOrder()!=0)) bForfend=false;
		if ((bForfend==true)&&(pISendUserItem->GetUserStatus()!=US_PLAYING)) bForfend=false;
		if ((bForfend==true)&&(pIRecvUserItem->GetUserStatus()!=US_PLAYING)) bForfend=false;
		if ((bForfend==true)&&((wTableIDSend==INVALID_TABLE)||(wTableIDSend!=wTableIDRecv))) bForfend=false;

		//提示消息
		if (bForfend==true)
		{
			SendRoomMessage(pISendUserItem,TEXT("抱歉，此游戏房间不允许在游戏中与同桌的玩家私聊！"),SMT_EJECT|SMT_CHAT);
			return true;
		}
	}

	//权限判断
	if (CUserRight::CanWisper(pISendUserItem->GetUserRight())==false)
	{
		SendRoomMessage(pISendUserItem,TEXT("抱歉，您没有发送私聊的权限，若需要帮助，请联系游戏客服咨询！"),SMT_EJECT|SMT_CHAT);
		return true;
	}

	//变量定义
	CMD_GR_S_WisperExpression WisperExpression;
	ZeroMemory(&WisperExpression,sizeof(WisperExpression));

	//构造数据
	WisperExpression.wItemIndex=pWisperExpression->wItemIndex;
	WisperExpression.dwSendUserID=pISendUserItem->GetUserID();
	WisperExpression.dwTargetUserID=pWisperExpression->dwTargetUserID;

	//转发消息
	SendData(pISendUserItem,MDM_GR_USER,SUB_GR_WISPER_EXPRESSION,&WisperExpression,sizeof(WisperExpression));
	SendData(pIRecvUserItem,MDM_GR_USER,SUB_GR_WISPER_EXPRESSION,&WisperExpression,sizeof(WisperExpression));

	return true;
}

//购买道具
bool CAttemperEngineSink::OnTCPNetworkSubPropertyBuy(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//事件处理
	int cbResult = OnPropertyBuy(pData, wDataSize, dwSocketID);

	//结果判断
	if(cbResult == RESULT_ERROR) return false;
	if(cbResult == RESULT_FAIL) return true;
	if(cbResult == RESULT_SUCCESS) return true;

	return true;
}

//购买道具
int CAttemperEngineSink::OnPropertyBuy(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_C_PropertyBuy));
	if (wDataSize!=sizeof(CMD_GR_C_PropertyBuy)) return RESULT_ERROR;

	//变量定义
	CMD_GR_C_PropertyBuy * pPropertyBuy=(CMD_GR_C_PropertyBuy *)pData;

	//数据效验
	ASSERT(pPropertyBuy->wItemCount>0);
	if (pPropertyBuy->wItemCount==0) return RESULT_ERROR;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return RESULT_ERROR;

	//目标用户
	IServerUserItem * pITargerUserItem=m_ServerUserManager.SearchUserItem(pPropertyBuy->dwTargetUserID);
	if (pITargerUserItem==NULL) return RESULT_ERROR;

	//比赛房间
	if (m_pGameServiceOption->wServerType==GAME_GENRE_MATCH)
	{
		//发送消息
		SendPropertyFailure(pIServerUserItem,TEXT("比赛房间不可以使用此功能！"),0L,pPropertyBuy->cbRequestArea);

		return RESULT_FAIL;
	}

	//练习房间
	if (m_pGameServiceOption->wServerType==GAME_GENRE_EDUCATE)
	{
		SendPropertyFailure(pIServerUserItem,TEXT("练习房间不可以使用此功能！"),0L,pPropertyBuy->cbRequestArea);
		return RESULT_FAIL;
	}

	//购前事件
	if(OnEventPropertyBuyPrep(pPropertyBuy->cbRequestArea,pPropertyBuy->wPropertyIndex,pIServerUserItem,pITargerUserItem)==false)
		return RESULT_FAIL;

	//变量定义
	DBR_GR_PropertyRequest PropertyRequest;
	ZeroMemory(&PropertyRequest,sizeof(PropertyRequest));

	//查找道具
	tagPropertyInfo * pPropertyInfo=m_GamePropertyManager.SearchPropertyItem(pPropertyBuy->wPropertyIndex);
	if (pPropertyInfo==NULL)
	{
		SendPropertyFailure(pIServerUserItem,TEXT("您购买的道具不存在或在维护中，请与管理员联系！"),0L,pPropertyBuy->cbRequestArea);
		return RESULT_FAIL;
	}

	//消费方式
	if (pPropertyBuy->cbConsumeScore==TRUE)
	{
		//房间判断
		ASSERT((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)!=0);
		if ((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)==0) return RESULT_FAIL;		

		//锁定计算
		PropertyRequest.cbConsumeScore=TRUE;
		PropertyRequest.lFrozenedScore=pPropertyInfo->lPropertyGold*pPropertyBuy->wItemCount;

		//会员折扣
		if (pIServerUserItem->GetMemberOrder()>0)
		{
			PropertyRequest.lFrozenedScore = PropertyRequest.lFrozenedScore*pPropertyInfo->wDiscount/100L;
		}

		//锁定积分
		if (pIServerUserItem->FrozenedUserScore(PropertyRequest.lFrozenedScore)==false)
		{
			SendPropertyFailure(pIServerUserItem,TEXT("您的游戏币余额不足，道具购买失败！"),0L,pPropertyBuy->cbRequestArea);
			return RESULT_FAIL;
		}
	}
	else
	{
		//银行扣费
		PropertyRequest.lFrozenedScore=0L;
		PropertyRequest.cbConsumeScore=FALSE;

		//变量定义
		SCORE lInsure = pIServerUserItem->GetUserInsure();
		SCORE lConsumeScore = pPropertyInfo->lPropertyGold*pPropertyBuy->wItemCount;

		//会员折扣
		if (pIServerUserItem->GetMemberOrder()>0)
		{
			lConsumeScore = lConsumeScore*pPropertyInfo->wDiscount/100L;
		}

		//银行校验
		if(lInsure < lConsumeScore)
		{
			SendPropertyFailure(pIServerUserItem,TEXT("您的保险柜余额不足，请存款后再次购买！"),0L,pPropertyBuy->cbRequestArea);
			return RESULT_FAIL;
		}		
	}

	//购买信息
	PropertyRequest.cbRequestArea=pPropertyBuy->cbRequestArea;
	PropertyRequest.wItemCount=pPropertyBuy->wItemCount;
	PropertyRequest.wPropertyIndex=pPropertyBuy->wPropertyIndex;
	PropertyRequest.dwSourceUserID=pIServerUserItem->GetUserID();
	PropertyRequest.dwTargetUserID=pITargerUserItem->GetUserID();

	//设置权限
	if(PropertyRequest.wPropertyIndex==PROPERTY_ID_VIPROOM_CARD)
		PropertyRequest.dwUserRight |= UR_GAME_KICK_OUT_USER|UR_GAME_ENTER_VIP_ROOM;
	else if(PropertyRequest.wPropertyIndex>=PROPERTY_ID_BLUERING_CARD && PropertyRequest.wPropertyIndex <=PROPERTY_ID_REDRING_CARD)
		PropertyRequest.dwUserRight |= UR_GAME_KICK_OUT_USER;
	else
		PropertyRequest.dwUserRight=0;

	//系统信息
	PropertyRequest.wTableID=INVALID_TABLE;
	PropertyRequest.dwInoutIndex=pIServerUserItem->GetInoutIndex();
	PropertyRequest.dwClientAddr=pIServerUserItem->GetClientAddr();
	lstrcpyn(PropertyRequest.szMachineID,pIServerUserItem->GetMachineID(),CountArray(PropertyRequest.szMachineID));

	//投递数据
	m_pIDBCorrespondManager->PostDataBaseRequest(pIServerUserItem->GetUserID(),DBR_GR_PROPERTY_REQUEST,dwSocketID,&PropertyRequest,sizeof(PropertyRequest));

	return RESULT_SUCCESS;
}

//使用道具
bool CAttemperEngineSink::OnTCPNetwordSubSendTrumpet(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_C_SendTrumpet));
	if (wDataSize!=sizeof(CMD_GR_C_SendTrumpet)) return false;

	//变量定义
	CMD_GR_C_SendTrumpet * pSendTrumpet=(CMD_GR_C_SendTrumpet *)pData;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);
	if(pIServerUserItem==NULL) return false;

	//构造结构
	CMD_GR_C_PropertyBuy  PropertyBuy;
	PropertyBuy.cbRequestArea=pSendTrumpet->cbRequestArea;
	PropertyBuy.dwTargetUserID=pIServerUserItem->GetUserID();
	PropertyBuy.wPropertyIndex=pSendTrumpet->wPropertyIndex;
	PropertyBuy.cbConsumeScore=FALSE;
	PropertyBuy.wItemCount=1;
    
	//购买
	int cbResult = OnPropertyBuy((void *)&PropertyBuy,sizeof(PropertyBuy),dwSocketID);

	//结果判断
	if(cbResult == RESULT_ERROR) return false;
	if(cbResult != RESULT_SUCCESS) return true;

	//获取道具
	tagUserProperty * pUserProperty = pIServerUserItem->GetUserProperty();

	//道具索引
	BYTE cbIndex=(pSendTrumpet->wPropertyIndex==PROPERTY_ID_TRUMPET)?2:3;

	//构造结构
	CMD_GR_S_SendTrumpet  SendTrumpet;
	SendTrumpet.dwSendUserID=pIServerUserItem->GetUserID();
	SendTrumpet.wPropertyIndex=pSendTrumpet->wPropertyIndex;
	SendTrumpet.TrumpetColor=pSendTrumpet->TrumpetColor;
	ZeroMemory(SendTrumpet.szTrumpetContent,sizeof(SendTrumpet.szTrumpetContent));
	CopyMemory(SendTrumpet.szSendNickName,pIServerUserItem->GetNickName(),sizeof(SendTrumpet.szSendNickName));

	//字符过滤
	SensitiveWordFilter(pSendTrumpet->szTrumpetContent,SendTrumpet.szTrumpetContent,CountArray(SendTrumpet.szTrumpetContent));

	DWORD dwSendUserAreaID = pIServerUserItem->GetAreaID();
    //房间转发
	if(cbIndex==3)
	{
		//广播房间
		WORD wUserIndex=0;
//		CMD_CS_S_SendTrumpet * pSendTrumpet = (CMD_CS_S_SendTrumpet *)&SendTrumpet;
		CMD_CS_S_SendTrumpet CSSendTrumpet;
		CSSendTrumpet.wPropertyIndex = SendTrumpet.wPropertyIndex;							//道具索引 
		CSSendTrumpet.dwSendUserID = SendTrumpet.dwSendUserID;								//用户 I D
		CSSendTrumpet.dwSendUserAreaID = dwSendUserAreaID;						//发送用户的区域ID
		CSSendTrumpet.TrumpetColor = SendTrumpet.TrumpetColor;								//喇叭颜色
		_tcsncpy(CSSendTrumpet.szSendNickName, SendTrumpet.szSendNickName, sizeof(CSSendTrumpet.szSendNickName));
		_tcsncpy(CSSendTrumpet.szTrumpetContent, SendTrumpet.szTrumpetContent, sizeof(CSSendTrumpet.szTrumpetContent));
		if(m_pITCPSocketService)
		{
			m_pITCPSocketService->SendData(MDM_CS_MANAGER_SERVICE, SUB_CS_C_PROPERTY_TRUMPET, &CSSendTrumpet, sizeof(CSSendTrumpet));
		}
	}

	//游戏转发
	if (cbIndex == 2)
	{
		//发送数据
		WORD wEnumIndex = 0;
		while (wEnumIndex < m_ServerUserManager.GetUserItemCount())
		{
			IServerUserItem* pIUserItem = m_ServerUserManager.EnumUserItem(wEnumIndex++);
			DWORD dwAreaID = pIUserItem->GetAreaID();
			if (pIUserItem == NULL)
			{
				continue;
			}
			if (dwSendUserAreaID == 0 || dwAreaID == dwSendUserAreaID)
			{
				WORD wBindIndex = pIUserItem->GetBindIndex();
				tagBindParameter * pBindParameter = GetBindParameter(wBindIndex);
				if (pBindParameter)
				{
					m_pITCPNetworkEngine->SendData(pBindParameter->dwSocketID, MDM_GR_USER, SUB_GR_PROPERTY_TRUMPET, &SendTrumpet, sizeof(SendTrumpet));
				}
			}
		}
	}

	return true;
}

bool CAttemperEngineSink::OnTCPAndroidSendTrumpet()
{
	//获取路径
	TCHAR szWorkDir[MAX_PATH]=TEXT("");
	CWHService::GetWorkDirectory(szWorkDir,CountArray(szWorkDir));

	//构造路径
	TCHAR szIniFile[MAX_PATH]=TEXT("");
	_sntprintf(szIniFile,CountArray(szIniFile),TEXT("%s\\AndroidMessageConfig.ini"),szWorkDir);

	//读取配置
	CWHIniData IniData;
	IniData.SetIniFilePath(szIniFile);
	if(m_nAndroidUserMessageCount!=0)
		m_nAndroidUserSendedCount=rand()%m_nAndroidUserMessageCount;
	TCHAR szMessage[255];
	TCHAR szName[32];
	CString str;
	str.Format(TEXT("Msg%d"),m_nAndroidUserSendedCount);
	IniData.ReadString(TEXT("Message"),str,NULL,szMessage,CountArray(szMessage));
	str.Format(TEXT("Name%d"),m_nAndroidUserSendedCount);
	IniData.ReadString(TEXT("NickName"),str,NULL,szName,CountArray(szName));
	//构造结构
	CMD_GR_S_SendTrumpet  SendTrumpet;
	SendTrumpet.dwSendUserID=201;
	SendTrumpet.wPropertyIndex=19;
		if((rand()%3)==1)
			SendTrumpet.TrumpetColor=RGB(255,0,0);
		else if((rand()%3)==2)
			SendTrumpet.TrumpetColor=RGB(51,169,57);
		else if((rand()%3)==0)
			SendTrumpet.TrumpetColor=RGB(168,117,132);
	ZeroMemory(SendTrumpet.szTrumpetContent,sizeof(SendTrumpet.szTrumpetContent));
	CopyMemory(SendTrumpet.szSendNickName,szName,sizeof(SendTrumpet.szSendNickName));
	//字符过滤
	SensitiveWordFilter(szMessage,SendTrumpet.szTrumpetContent,CountArray(szMessage));

	m_pITCPNetworkEngine->SendDataBatch(MDM_GR_USER,SUB_GR_PROPERTY_TRUMPET,&SendTrumpet,sizeof(SendTrumpet),BG_COMPUTER);
	//m_nAndroidUserSendedCount++;
	return true;

}

//邀请用户
bool CAttemperEngineSink::OnTCPNetworkSubUserInviteReq(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验数据
	ASSERT(wDataSize==sizeof(CMD_GR_UserInviteReq));
	if (wDataSize!=sizeof(CMD_GR_UserInviteReq)) return false;

	//消息处理
	CMD_GR_UserInviteReq * pUserInviteReq=(CMD_GR_UserInviteReq *)pData;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);
	if (pIServerUserItem==NULL) return false;

	//效验状态
	if (pUserInviteReq->wTableID==INVALID_TABLE) return true;
	if (pIServerUserItem->GetTableID()!=pUserInviteReq->wTableID) return true;
	if (pIServerUserItem->GetUserStatus()==US_PLAYING) return true;
	if (pIServerUserItem->GetUserStatus()==US_OFFLINE) return true;

	//目标用户
	IServerUserItem * pITargetUserItem=m_ServerUserManager.SearchUserItem(pUserInviteReq->dwUserID);
	if (pITargetUserItem==NULL) return true;
	if (pITargetUserItem->GetUserStatus()==US_PLAYING) return true;

	//发送消息
	CMD_GR_UserInvite UserInvite;
	memset(&UserInvite,0,sizeof(UserInvite));
	UserInvite.wTableID=pUserInviteReq->wTableID;
	UserInvite.dwUserID=pIServerUserItem->GetUserID();
	SendData(pITargetUserItem,MDM_GR_USER,SUB_GR_USER_INVITE,&UserInvite,sizeof(UserInvite));

	return true;
}

//拒绝厌友
bool CAttemperEngineSink::OnTCPNetworkSubUserRepulseSit(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验数据
	ASSERT(wDataSize==sizeof(CMD_GR_UserRepulseSit));
	if (wDataSize!=sizeof(CMD_GR_UserRepulseSit)) return false;

	//消息处理
	CMD_GR_UserRepulseSit * pUserRepulseSit=(CMD_GR_UserRepulseSit *)pData;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);
	if (pIServerUserItem==NULL) return false;

	//获取桌子
	CTableFrame * pTableFrame=m_TableFrameArray[pUserRepulseSit->wTableID];
	if (pTableFrame->IsGameStarted()==true) return true;

	//获取用户
	IServerUserItem * pRepulseIServerUserItem = pTableFrame->GetTableUserItem(pUserRepulseSit->wChairID);
	if (pRepulseIServerUserItem==NULL) return true;
	if(pRepulseIServerUserItem->GetUserID() != pUserRepulseSit->dwRepulseUserID)return true;

	//发送消息
	TCHAR szDescribe[256]=TEXT("");
	lstrcpyn(szDescribe,TEXT("此桌有玩家设置了不与您同桌游戏！"),CountArray(szDescribe));
	SendRoomMessage(pRepulseIServerUserItem,szDescribe,SMT_EJECT|SMT_CHAT|SMT_CLOSE_GAME);

	//弹起玩家
	LOGI("CAttemperEngineSink::OnTCPNetworkSubUserRepulseSit PerformStandUpAction, NickName="<<pRepulseIServerUserItem->GetNickName());
	pTableFrame->PerformStandUpAction(pRepulseIServerUserItem);

	return true;
}

//踢出命令
bool CAttemperEngineSink::OnTCPNetworkSubMemberKickUser(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_KickUser));
	if (wDataSize!=sizeof(CMD_GR_KickUser)) return false;

	//变量定义
	CMD_GR_KickUser * pKickUser=(CMD_GR_KickUser *)pData;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//目标用户
	IServerUserItem * pITargetUserItem = m_ServerUserManager.SearchUserItem(pKickUser->dwTargetUserID);
	if(pITargetUserItem==NULL) return true;

	//用户效验
	ASSERT((pIServerUserItem!=NULL)&&(pIServerUserItem->GetMemberOrder()>pITargetUserItem->GetMemberOrder()));
	if ((pIServerUserItem==NULL)||(pIServerUserItem->GetMemberOrder()<=pITargetUserItem->GetMemberOrder())) return false;

	//权限判断
	ASSERT(CUserRight::CanKillOutUser(pIServerUserItem->GetUserRight())==true);
	if (CUserRight::CanKillOutUser(pIServerUserItem->GetUserRight())==false) return false;

	//百人游戏
	if(m_pGameServiceAttrib->wChairCount >= MAX_CHAIR)
	{
		//发送消息
		SendRoomMessage(pIServerUserItem,TEXT("很抱歉，百人游戏不许踢人！"),SMT_EJECT);
		return true;
	}

	//用户状态
	if(pITargetUserItem->GetUserStatus()==US_PLAYING)
	{
		//变量定义
		TCHAR szMessage[256]=TEXT("");
		_sntprintf(szMessage,CountArray(szMessage),TEXT("由于玩家 [ %s ] 正在游戏中,您不能将它踢出游戏！"),pITargetUserItem->GetNickName());

		//发送消息
		SendRoomMessage(pIServerUserItem,szMessage,SMT_EJECT);
		return true;
	}

	//防踢判断
	if((pITargetUserItem->GetUserProperty()->wPropertyUseMark&PT_USE_MARK_GUARDKICK_CARD)!=0)
	{
		//变量定义
		DWORD dwCurrentTime=(DWORD)time(NULL);
		tagUserProperty * pUserProperty = pITargetUserItem->GetUserProperty();

		//时效判断
		DWORD dwValidTime=pUserProperty->PropertyInfo[2].wPropertyCount*pUserProperty->PropertyInfo[2].dwValidNum;
		if(pUserProperty->PropertyInfo[2].dwEffectTime+dwValidTime>dwCurrentTime)
		{
			//变量定义
			TCHAR szMessage[256]=TEXT("");
			_sntprintf(szMessage,CountArray(szMessage),TEXT("由于玩家 [ %s ] 正在使用防踢卡,您无法将它踢出游戏！"),pITargetUserItem->GetNickName());

			//发送消息
			SendRoomMessage(pIServerUserItem,szMessage,SMT_EJECT);

			return true; 
		}
		else
			pUserProperty->wPropertyUseMark &= ~PT_USE_MARK_GUARDKICK_CARD;
	}

	//请离桌子
	WORD wTargerTableID = pITargetUserItem->GetTableID();
	if(wTargerTableID != INVALID_TABLE)
	{
		//定义变量
		TCHAR szMessage[64]=TEXT("");
		_sntprintf(szMessage,CountArray(szMessage),TEXT("你已被%s请离桌子！"),pIServerUserItem->GetNickName());

		//发送消息
		SendGameMessage(pITargetUserItem,szMessage,SMT_CHAT|SMT_CLOSE_GAME);

		CTableFrame * pTableFrame=m_TableFrameArray[wTargerTableID];
		LOGI("CAttemperEngineSink::OnTCPNetworkSubMemberKickUser PerformStandUpAction, NickName="<<pITargetUserItem->GetNickName());
		if (pTableFrame->PerformStandUpAction(pITargetUserItem)==false) return true;
	}

	return true;
}

//请求用户信息
bool CAttemperEngineSink::OnTCPNetworkSubUserInfoReq(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_UserInfoReq));
	if (wDataSize!=sizeof(CMD_GR_UserInfoReq)) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//变量定义
	CMD_GR_UserInfoReq * pUserInfoReq = (CMD_GR_UserInfoReq *)pData;
	WORD wCurDeskPos = pIServerUserItem->GetMobileUserDeskPos();
	WORD wMaxDeskPos = m_pGameServiceOption->wTableCount-pIServerUserItem->GetMobileUserDeskCount();

	//数量效验
	if(pUserInfoReq->wTablePos > wMaxDeskPos) pUserInfoReq->wTablePos = wMaxDeskPos;

	//更新信息
	pIServerUserItem->SetMobileUserDeskPos(pUserInfoReq->wTablePos);
	
	//发送信息
	SendViewTableUserInfoPacketToMobileUser(pIServerUserItem,pUserInfoReq->dwUserIDReq);

	return true;
}

//请求更换位置
bool CAttemperEngineSink::OnTCPNetworkSubUserChairReq(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//用户状态
	if (pIServerUserItem->GetUserStatus() == US_TRUSTEE)
	{
		SendRequestFailure(pIServerUserItem,TEXT("您正在托管代打，操作无效，请等待当前游戏结束！"),0);
		return true;
	}
	if(pIServerUserItem->GetUserStatus() == US_PLAYING)
	{
		//失败
		m_TableFrameArray[0]->SendRequestFailure(pIServerUserItem,TEXT("您正在游戏中，暂时不能离开，请先结束当前游戏！"),REQUEST_FAILURE_NORMAL);
		return true;
	}

	//查找桌子
	for (INT_PTR i=0;i<(m_pGameServiceOption->wTableCount);i++)
	{
		//过滤同桌
		if(i == pIServerUserItem->GetTableID())continue;

		//获取桌子
		CTableFrame * pTableFrame=m_TableFrameArray[i];
		if (pTableFrame->IsGameStarted()==true) continue;
		if(pTableFrame->IsTableLocked()) continue;

		//无效过滤
		WORD wChairID=pTableFrame->GetRandNullChairID();
		if (wChairID==INVALID_CHAIR) continue;

		//离开处理
		if (pIServerUserItem->GetTableID()!=INVALID_TABLE)
		{
			CTableFrame * pTableFrame=m_TableFrameArray[pIServerUserItem->GetTableID()];
			LOGI("CAttemperEngineSink::OnTCPNetworkSubUserChairReq PerformStandUpAction, NickName="<<pIServerUserItem->GetNickName());
			if (pTableFrame->PerformStandUpAction(pIServerUserItem)==false) return true;
		}

		//定义变量
		WORD wTagerDeskPos = pIServerUserItem->GetMobileUserDeskPos();
		WORD wTagerDeskCount = pIServerUserItem->GetMobileUserDeskCount();

		//更新信息
		if((i < wTagerDeskPos) ||(i > (wTagerDeskPos+wTagerDeskCount-1)))
		{
			pIServerUserItem->SetMobileUserDeskPos(i/wTagerDeskCount);
		}

		//用户坐下
		pTableFrame->PerformSitDownAction(wChairID,pIServerUserItem);
		return true;
	}

	//查找同桌
	if(pIServerUserItem->GetTableID() != INVALID_TABLE)
	{
		//获取桌子
		CTableFrame * pTableFrame=m_TableFrameArray[pIServerUserItem->GetTableID()];
		if (pTableFrame->IsGameStarted()==false && pTableFrame->IsTableLocked()==false)
		{
			//无效过滤
			WORD wChairID=pTableFrame->GetRandNullChairID();
			if (wChairID!=INVALID_CHAIR)
			{
				//离开处理
				if (pIServerUserItem->GetTableID()!=INVALID_TABLE)
				{
					CTableFrame * pTableFrame=m_TableFrameArray[pIServerUserItem->GetTableID()];
					LOGI("CAttemperEngineSink::OnTCPNetworkSubUserChairReq PerformStandUpAction, (pIServerUserItem->GetTableID() != INVALID_TABLE), NickName="<<pIServerUserItem->GetNickName());
					if (pTableFrame->PerformStandUpAction(pIServerUserItem)==false) return true;
				}

				//用户坐下
				pTableFrame->PerformSitDownAction(wChairID,pIServerUserItem);
				return true;
			}
		}
	}

	//失败
	m_TableFrameArray[0]->SendRequestFailure(pIServerUserItem,TEXT("没找到可进入的游戏桌！"),REQUEST_FAILURE_NORMAL);
	return true;
}

//请求椅子用户信息
bool CAttemperEngineSink::OnTCPNetworkSubChairUserInfoReq(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_ChairUserInfoReq));
	if (wDataSize!=sizeof(CMD_GR_ChairUserInfoReq)) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//变量定义
	CMD_GR_ChairUserInfoReq * pUserInfoReq = (CMD_GR_ChairUserInfoReq *)pData;
	if(pUserInfoReq->wTableID == INVALID_TABLE) return true;
	if(pUserInfoReq->wTableID >= m_pGameServiceOption->wTableCount)return true;

	//发送消息
	WORD wChairCout = m_TableFrameArray[pUserInfoReq->wTableID]->GetChairCount();
	for(WORD wIndex = 0; wIndex < wChairCout; wIndex++)
	{
		//获取用户
		if(pUserInfoReq->wChairID != INVALID_CHAIR && wIndex != pUserInfoReq->wChairID)continue;
		IServerUserItem * pTagerIServerUserItem=m_TableFrameArray[pUserInfoReq->wTableID]->GetTableUserItem(wIndex);
		if(pTagerIServerUserItem==NULL)continue;

		//变量定义
		BYTE cbBuffer[SOCKET_TCP_PACKET]={0};
		tagMobileUserInfoHead * pUserInfoHead=(tagMobileUserInfoHead *)cbBuffer;
		CSendPacketHelper SendPacket(cbBuffer+sizeof(tagMobileUserInfoHead),sizeof(cbBuffer)-sizeof(tagMobileUserInfoHead));
		tagUserInfo *pUserInfo = pTagerIServerUserItem->GetUserInfo();

		//用户属性
		pUserInfoHead->wFaceID=pUserInfo->wFaceID;
		pUserInfoHead->dwGameID=pUserInfo->dwGameID;
		pUserInfoHead->dwUserID=pUserInfo->dwUserID;
		pUserInfoHead->dwCustomID=pUserInfo->dwCustomID;

		//用户属性
		pUserInfoHead->cbGender=pUserInfo->cbGender;
		pUserInfoHead->cbMemberOrder=pUserInfo->cbMemberOrder;

		//用户状态
		pUserInfoHead->wTableID=pUserInfo->wTableID;
		pUserInfoHead->wChairID=pUserInfo->wChairID;
		pUserInfoHead->cbUserStatus=pUserInfo->cbUserStatus;

		//用户局数
		pUserInfoHead->dwWinCount=pUserInfo->dwWinCount;
		pUserInfoHead->dwLostCount=pUserInfo->dwLostCount;
		pUserInfoHead->dwDrawCount=pUserInfo->dwDrawCount;
		pUserInfoHead->dwFleeCount=pUserInfo->dwFleeCount;
		pUserInfoHead->dwExperience=pUserInfo->dwExperience;

		//用户游戏币
		pUserInfoHead->lScore=pUserInfo->lScore;
		pUserInfoHead->lScore+=pIServerUserItem->GetTrusteeScore();
		pUserInfoHead->lScore+=pIServerUserItem->GetFrozenedScore();

		//叠加信息
		SendPacket.AddPacket(pUserInfo->szNickName,DTP_GR_NICK_NAME);

		//发送消息
		WORD wHeadSize=sizeof(tagMobileUserInfoHead);
		SendData(pIServerUserItem,MDM_GR_USER,SUB_GR_USER_ENTER,cbBuffer,wHeadSize+SendPacket.GetDataSize());
	}

	return true;
}

//查询银行
bool CAttemperEngineSink::OnTCPNetworkSubQueryInsureInfo(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_C_QueryInsureInfoRequest));
	if (wDataSize!=sizeof(CMD_GR_C_QueryInsureInfoRequest)) return false;

	//房间判断
	ASSERT((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)||(m_pGameServiceOption->wServerType&GAME_GENRE_MATCH));
	if ((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)==0&&(m_pGameServiceOption->wServerType&GAME_GENRE_MATCH)==0) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//变量定义
	CMD_GR_C_QueryInsureInfoRequest * pQueryInsureInfoRequest = (CMD_GR_C_QueryInsureInfoRequest *)pData;

	//变量定义
	DBR_GR_QueryInsureInfo QueryInsureInfo;
	ZeroMemory(&QueryInsureInfo,sizeof(QueryInsureInfo));

	//构造数据
	QueryInsureInfo.cbActivityGame=pQueryInsureInfoRequest->cbActivityGame;
	QueryInsureInfo.dwUserID=pIServerUserItem->GetUserID();
	QueryInsureInfo.dwClientAddr=pIServerUserItem->GetClientAddr();

	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(QueryInsureInfo.dwUserID,DBR_GR_QUERY_INSURE_INFO,dwSocketID,&QueryInsureInfo,sizeof(QueryInsureInfo));

	return true;
}
bool CAttemperEngineSink::OnTCPNetworkSubQueryTransRecord(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
    //效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_C_QueryInsureInfoRequest));
	if (wDataSize!=sizeof(CMD_GR_C_QueryInsureInfoRequest)) return false;

	//房间判断
	ASSERT((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)!=0);
	if ((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)==0) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//变量定义
	CMD_GR_C_QueryInsureInfoRequest * pQueryInsureInfoRequest = (CMD_GR_C_QueryInsureInfoRequest *)pData;

	//变量定义
	DBR_GR_QueryInsureInfo QueryInsureInfo;
	ZeroMemory(&QueryInsureInfo,sizeof(QueryInsureInfo));

	//构造数据
	QueryInsureInfo.cbActivityGame=pQueryInsureInfoRequest->cbActivityGame;
	QueryInsureInfo.dwUserID=pIServerUserItem->GetUserID();
	QueryInsureInfo.dwClientAddr=pIServerUserItem->GetClientAddr();

	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(QueryInsureInfo.dwUserID,DBR_GR_QUERY_TRANSRECORD,dwSocketID,&QueryInsureInfo,sizeof(QueryInsureInfo));
    return true;
}
//存款请求
bool CAttemperEngineSink::OnTCPNetworkSubSaveScoreRequest(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_C_SaveScoreRequest));
	if (wDataSize!=sizeof(CMD_GR_C_SaveScoreRequest)) return false;

	//房间判断
	ASSERT((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)||(m_pGameServiceOption->wServerType&GAME_GENRE_MATCH));
	if ((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)==0&&(m_pGameServiceOption->wServerType&GAME_GENRE_MATCH)==0) return false;

	//变量定义
	CMD_GR_C_SaveScoreRequest * pSaveScoreRequest=(CMD_GR_C_SaveScoreRequest *)pData;

	//效验参数
	ASSERT(pSaveScoreRequest->lSaveScore>0L);
	if (pSaveScoreRequest->lSaveScore<=0L) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//规则判断
	if(pSaveScoreRequest->cbActivityGame == FALSE && CServerRule::IsForfendSaveInRoom(m_pGameServiceOption->dwServerRule))
	{
		//发送数据
		SendInsureFailure(pIServerUserItem,TEXT("此房间禁止房间存款，存入操作失败！"),0L,pSaveScoreRequest->cbActivityGame);
		return true;
	}

	if (pIServerUserItem->GetUserStatus() == US_TRUSTEE)
	{
		SendInsureFailure(pIServerUserItem,TEXT("由于您处于托管代打状态，不能进行存款，存入操作失败！"),0L,pSaveScoreRequest->cbActivityGame);
		return true;
	}

	// 如果在桌上，则不允许存钱
	if (pIServerUserItem->GetTableID()!=INVALID_TABLE && CServerRule::IsForfendSaveInGame(m_pGameServiceOption->dwServerRule) )
	{
		SendInsureFailure(pIServerUserItem,TEXT("由于您正在游戏中，不能进行存款，存入操作失败！"),0L,pSaveScoreRequest->cbActivityGame);
		return true;
	}

	//规则判断
	if(pSaveScoreRequest->cbActivityGame == TRUE && CServerRule::IsForfendSaveInGame(m_pGameServiceOption->dwServerRule))
	{
		//发送数据
		SendInsureFailure(pIServerUserItem,TEXT("此房间禁止游戏存款，存入操作失败！"),0L,pSaveScoreRequest->cbActivityGame);
		return true;
	}

	//变量定义
	SCORE lConsumeQuota=0L;
	SCORE lUserWholeScore=pIServerUserItem->GetUserScore()+pIServerUserItem->GetTrusteeScore();

	//获取限额
	if (pIServerUserItem->GetTableID()!=INVALID_TABLE)
	{
		WORD wTableID=pIServerUserItem->GetTableID();
		lConsumeQuota=m_TableFrameArray[wTableID]->QueryConsumeQuota(pIServerUserItem);
	}
	else
	{
		lConsumeQuota=pIServerUserItem->GetUserScore()+pIServerUserItem->GetTrusteeScore();
	}

	//限额判断
	if (pSaveScoreRequest->lSaveScore>lConsumeQuota)
	{
		if (lConsumeQuota<lUserWholeScore)
		{
			//构造提示
			TCHAR szDescribe[128]=TEXT("");
			_sntprintf(szDescribe,CountArray(szDescribe),TEXT("由于您正在游戏中，游戏币可存入额度为 %I64d，存入操作失败！"),lConsumeQuota);

			//发送数据
			SendInsureFailure(pIServerUserItem,szDescribe,0L,pSaveScoreRequest->cbActivityGame);
		}
		else
		{
			//发送数据
			SendInsureFailure(pIServerUserItem,TEXT("您的游戏币余额不足，存入操作失败！"),0L,pSaveScoreRequest->cbActivityGame);
		}

		return true;
	}

	//锁定积分
	if (pIServerUserItem->FrozenedUserScore(pSaveScoreRequest->lSaveScore)==false)
	{
		ASSERT(FALSE);
		return false;
	}

	//变量定义
	DBR_GR_UserSaveScore UserSaveScore;
	ZeroMemory(&UserSaveScore,sizeof(UserSaveScore));

	//构造数据
	UserSaveScore.cbActivityGame=pSaveScoreRequest->cbActivityGame;
	UserSaveScore.dwUserID=pIServerUserItem->GetUserID();
	UserSaveScore.lSaveScore=pSaveScoreRequest->lSaveScore;
	UserSaveScore.dwClientAddr=pIServerUserItem->GetClientAddr();
	lstrcpyn(UserSaveScore.szMachineID,pIServerUserItem->GetMachineID(),CountArray(UserSaveScore.szMachineID));

//	LOGI(_T("CAttemperEngineSink::OnTCPNetworkSubSaveScoreRequest"));
	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(pIServerUserItem->GetUserID(),DBR_GR_USER_SAVE_SCORE,dwSocketID,&UserSaveScore,sizeof(UserSaveScore));

	return true;
}

//取款请求
bool CAttemperEngineSink::OnTCPNetworkSubTakeScoreRequest(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_C_TakeScoreRequest));
	if (wDataSize!=sizeof(CMD_GR_C_TakeScoreRequest)) return false;

	//房间判断
	ASSERT((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)||(m_pGameServiceOption->wServerType&GAME_GENRE_MATCH));
	if ((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)==0&&(m_pGameServiceOption->wServerType&GAME_GENRE_MATCH)==0) return false;

	//变量定义
	CMD_GR_C_TakeScoreRequest * pTakeScoreRequest=(CMD_GR_C_TakeScoreRequest *)pData;
	pTakeScoreRequest->szInsurePass[CountArray(pTakeScoreRequest->szInsurePass)-1]=0;

	//效验参数
	ASSERT(pTakeScoreRequest->lTakeScore>0L);
	if (pTakeScoreRequest->lTakeScore<=0L) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//规则判断
	if(pTakeScoreRequest->cbActivityGame == FALSE && CServerRule::IsForfendTakeInRoom(m_pGameServiceOption->dwServerRule))
	{
		//发送数据
		SendInsureFailure(pIServerUserItem,TEXT("此房间禁止房间取款，取出操作失败！"),0L,pTakeScoreRequest->cbActivityGame);
		return true;
	}

	if (pIServerUserItem->GetUserStatus() == US_TRUSTEE)
	{
		SendInsureFailure(pIServerUserItem,TEXT("由于您处于托管代打状态，不能进行取款，取出操作失败！"), 0L, pTakeScoreRequest->cbActivityGame);
		return true;
	}

	// 如果在桌上，则不允许存钱
	if (pIServerUserItem->GetTableID()!=INVALID_TABLE && CServerRule::IsForfendSaveInGame(m_pGameServiceOption->dwServerRule) )
	{
		SendInsureFailure(pIServerUserItem,TEXT("由于您正在游戏中，不能进行取款，取出操作失败！"),0L,pTakeScoreRequest->cbActivityGame);
		return true;
	}

	//规则判断
	if(pTakeScoreRequest->cbActivityGame == TRUE && CServerRule::IsForfendTakeInGame(m_pGameServiceOption->dwServerRule))
	{
		//发送数据
		SendInsureFailure(pIServerUserItem,TEXT("此房间禁止游戏取款，取出操作失败！"),0L,pTakeScoreRequest->cbActivityGame);
		return true;
	}

	//变量定义
	DBR_GR_UserTakeScore UserTakeScore;
	ZeroMemory(&UserTakeScore,sizeof(UserTakeScore));

	//构造数据
	UserTakeScore.cbActivityGame=pTakeScoreRequest->cbActivityGame;
	UserTakeScore.dwUserID=pIServerUserItem->GetUserID();
	UserTakeScore.lTakeScore=pTakeScoreRequest->lTakeScore;
	UserTakeScore.dwClientAddr=pIServerUserItem->GetClientAddr();
	lstrcpyn(UserTakeScore.szPassword,pTakeScoreRequest->szInsurePass,CountArray(UserTakeScore.szPassword));
	lstrcpyn(UserTakeScore.szMachineID,pIServerUserItem->GetMachineID(),CountArray(UserTakeScore.szMachineID));

	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(pIServerUserItem->GetUserID(),DBR_GR_USER_TAKE_SCORE,dwSocketID,&UserTakeScore,sizeof(UserTakeScore));

	return true;
}

//转账请求
bool CAttemperEngineSink::OnTCPNetworkSubTransferScoreRequest(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GP_C_TransferScoreRequest));
	if (wDataSize!=sizeof(CMD_GP_C_TransferScoreRequest)) return false;

	//房间判断
	ASSERT((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)||(m_pGameServiceOption->wServerType&GAME_GENRE_MATCH));
	if ((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)==0&&(m_pGameServiceOption->wServerType&GAME_GENRE_MATCH)==0) return false;

	//变量定义
	CMD_GP_C_TransferScoreRequest * pTransferScoreRequest=(CMD_GP_C_TransferScoreRequest *)pData;
	pTransferScoreRequest->szNickName[CountArray(pTransferScoreRequest->szNickName)-1]=0;
	pTransferScoreRequest->szInsurePass[CountArray(pTransferScoreRequest->szInsurePass)-1]=0;

	//效验参数
	ASSERT(pTransferScoreRequest->lTransferScore>0L);
	if (pTransferScoreRequest->lTransferScore<=0L) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	// 游戏中无法转账，房间中也无法转账
	if ( pTransferScoreRequest->cbActivityGame==FALSE && CServerRule::IsForfendTransferInRoom(m_pGameServiceOption->dwServerRule) )
	{
		//发送数据
		SendInsureFailure(pIServerUserItem,TEXT("此房间禁止房间转账，转账操作失败！"),0L,pTransferScoreRequest->cbActivityGame);
		return true;
	}

	if (pIServerUserItem->GetUserStatus() == US_TRUSTEE)
	{
		SendInsureFailure(pIServerUserItem,TEXT("由于您处于托管代打状态，不能进行转账，转账操作失败！"),0L,pTransferScoreRequest->cbActivityGame);
		return true;
	}

	// 如果在桌上，则不允许存钱
	if (pIServerUserItem->GetTableID()!=INVALID_TABLE && CServerRule::IsForfendSaveInGame(m_pGameServiceOption->dwServerRule) )
	{
		SendInsureFailure(pIServerUserItem,TEXT("由于您正在游戏中，不能进行转账，转账操作失败！"),0L,pTransferScoreRequest->cbActivityGame);
		return true;
	}

	if ( pTransferScoreRequest->cbActivityGame==TRUE && CServerRule::IsForfendTransferInGame(m_pGameServiceOption->dwServerRule) )
	{
		//发送数据
		SendInsureFailure(pIServerUserItem,TEXT("此房间禁止游戏转账，转账操作失败！"),0L,pTransferScoreRequest->cbActivityGame);
		return true;
	}

	//变量定义
	DBR_GR_UserTransferScore UserTransferScore;
	ZeroMemory(&UserTransferScore,sizeof(UserTransferScore));

	//构造数据
	UserTransferScore.cbActivityGame=pTransferScoreRequest->cbActivityGame;
	UserTransferScore.dwUserID=pIServerUserItem->GetUserID();
	UserTransferScore.dwClientAddr=pIServerUserItem->GetClientAddr();
	UserTransferScore.cbByNickName=pTransferScoreRequest->cbByNickName;
	UserTransferScore.lTransferScore=pTransferScoreRequest->lTransferScore;
	lstrcpyn(UserTransferScore.szNickName,pTransferScoreRequest->szNickName,CountArray(UserTransferScore.szNickName));
	lstrcpyn(UserTransferScore.szMachineID,pIServerUserItem->GetMachineID(),CountArray(UserTransferScore.szMachineID));
	lstrcpyn(UserTransferScore.szPassword,pTransferScoreRequest->szInsurePass,CountArray(UserTransferScore.szPassword));

//	LOGI(_T("CAttemperEngineSink::OnTCPNetworkSubTransferScoreRequest"));
	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(pIServerUserItem->GetUserID(),DBR_GR_USER_TRANSFER_SCORE,dwSocketID,&UserTransferScore,sizeof(UserTransferScore));

	return true;
}

//查询用户请求
bool CAttemperEngineSink::OnTCPNetworkSubQueryUserInfoRequest(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_C_QueryUserInfoRequest));
	if (wDataSize!=sizeof(CMD_GR_C_QueryUserInfoRequest)) return false;

	//房间判断
	ASSERT((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)!=0);
	if ((m_pGameServiceOption->wServerType&GAME_GENRE_GOLD)==0) return false;

	//变量定义
	CMD_GR_C_QueryUserInfoRequest * pQueryUserInfoRequest=(CMD_GR_C_QueryUserInfoRequest *)pData;
	pQueryUserInfoRequest->szNickName[CountArray(pQueryUserInfoRequest->szNickName)-1]=0;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//变量定义
	DBR_GR_QueryTransferUserInfo QueryTransferUserInfo;
	ZeroMemory(&QueryTransferUserInfo,sizeof(QueryTransferUserInfo));

	//构造数据
	QueryTransferUserInfo.cbActivityGame=pQueryUserInfoRequest->cbActivityGame;
	QueryTransferUserInfo.cbByNickName=pQueryUserInfoRequest->cbByNickName;
	lstrcpyn(QueryTransferUserInfo.szNickName,pQueryUserInfoRequest->szNickName,CountArray(QueryTransferUserInfo.szNickName));
	QueryTransferUserInfo.dwUserID=pIServerUserItem->GetUserID();

	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(pIServerUserItem->GetUserID(),DBR_GR_QUERY_TRANSFER_USER_INFO,dwSocketID,&QueryTransferUserInfo,sizeof(QueryTransferUserInfo));

	return true;
}

// 根据GameID查询昵称
bool CAttemperEngineSink::OnTCPNetworkSubQueryNickNameByGameID(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GP_QueryNickNameByGameID));
	if (wDataSize!=sizeof(CMD_GP_QueryNickNameByGameID)) return false;

	//处理消息
	CMD_GP_QueryNickNameByGameID* pQueryNickNameByGameID=(CMD_GP_QueryNickNameByGameID *)pData;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//变量定义
	DBR_GR_QueryNickNameByGameID QueryNickNameByGameID;
	ZeroMemory(&QueryNickNameByGameID,sizeof(QueryNickNameByGameID));


	//构造数据
	QueryNickNameByGameID.dwUserID=pIServerUserItem->GetUserID();
	QueryNickNameByGameID.dwGameID=pQueryNickNameByGameID->dwGameID;

	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(pIServerUserItem->GetUserID(), DBR_GR_QUERY_NICKNAME_BY_GAMEID,dwSocketID,&QueryNickNameByGameID,sizeof(QueryNickNameByGameID));
	return true;
}

// 验证银行密码
bool CAttemperEngineSink::OnTCPNetworkSubVerifyInsurePassword(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_VerifyUserInsurePassword));
	if (wDataSize!=sizeof(CMD_GR_VerifyUserInsurePassword)) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);
	
	//用户效验
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//处理消息
	CMD_GR_VerifyUserInsurePassword* pVerifyUserInsurePassword=(CMD_GR_VerifyUserInsurePassword *)pData;

	//变量定义
	DBR_GR_VerifyInsurePassword VerifyInsurePassword;
	ZeroMemory(&VerifyInsurePassword,sizeof(VerifyInsurePassword));

	//构造数据
	VerifyInsurePassword.dwUserID=pVerifyUserInsurePassword->dwUserID;
	lstrcpyn(VerifyInsurePassword.szInsurePassword,pVerifyUserInsurePassword->szInsurePassword,CountArray(VerifyInsurePassword.szInsurePassword));

	m_pIDBCorrespondManager->PostDataBaseRequest(pIServerUserItem->GetUserID(), DBR_GR_VERIFY_INSURE_PASSWORD,dwSocketID,&VerifyInsurePassword,sizeof(VerifyInsurePassword));
	return true;
}

// 更改银行密码
bool CAttemperEngineSink::OnTCPNetworkSubModifyInsurePassword(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GP_ModifyInsurePass));
	if (wDataSize!=sizeof(CMD_GP_ModifyInsurePass)) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//处理消息
	CMD_GP_ModifyInsurePass * pModifyInsurePass=(CMD_GP_ModifyInsurePass *)pData;
	pModifyInsurePass->szDesPassword[CountArray(pModifyInsurePass->szDesPassword)-1]=0;
	pModifyInsurePass->szScrPassword[CountArray(pModifyInsurePass->szScrPassword)-1]=0;

	//变量定义
	DBR_GR_ModifyInsurePass ModifyInsurePass;
	ZeroMemory(&ModifyInsurePass,sizeof(ModifyInsurePass));

	//构造数据
	ModifyInsurePass.dwUserID=pModifyInsurePass->dwUserID;
	ModifyInsurePass.dwClientAddr=pIServerUserItem->GetClientAddr();
	lstrcpyn(ModifyInsurePass.szDesPassword,pModifyInsurePass->szDesPassword,CountArray(ModifyInsurePass.szDesPassword));
	lstrcpyn(ModifyInsurePass.szScrPassword,pModifyInsurePass->szScrPassword,CountArray(ModifyInsurePass.szScrPassword));

	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(pIServerUserItem->GetUserID(), DBR_GR_MODIFY_INSURE_PASSWORD,dwSocketID,&ModifyInsurePass,sizeof(ModifyInsurePass));

	return true;
}

//查询设置
bool CAttemperEngineSink::OnTCPNetworkSubQueryOption(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT((pIServerUserItem!=NULL)&&(pIServerUserItem->GetMasterOrder()>0));
	if ((pIServerUserItem==NULL)||(pIServerUserItem->GetMasterOrder()==0)) return false;

	//变量定义
	CMD_GR_OptionCurrent OptionCurrent;
	ZeroMemory(&OptionCurrent,sizeof(OptionCurrent));

	//挂接属性
	OptionCurrent.ServerOptionInfo.wKindID=m_pGameServiceOption->wKindID;
	OptionCurrent.ServerOptionInfo.wNodeID=m_pGameServiceOption->wNodeID;
	OptionCurrent.ServerOptionInfo.wSortID=m_pGameServiceOption->wSortID;

	//房间配置
#ifndef _READ_TABLE_OPTION_
	OptionCurrent.ServerOptionInfo.wRevenueRatio=m_pGameServiceOption->wRevenueRatio;
	OptionCurrent.ServerOptionInfo.lServiceScore=m_pGameServiceOption->lServiceScore;
	OptionCurrent.ServerOptionInfo.lRestrictScore=m_pGameServiceOption->lRestrictScore;
	OptionCurrent.ServerOptionInfo.lMinTableScore=m_pGameServiceOption->lMinTableScore;
#else
	// anjay !!
	// GM配置暂时失效,需要修改消息协议
#endif
	OptionCurrent.ServerOptionInfo.lMinEnterScore=m_pGameServiceOption->lMinEnterScore;
	OptionCurrent.ServerOptionInfo.lMaxEnterScore=m_pGameServiceOption->lMaxEnterScore;

	//会员限制
	OptionCurrent.ServerOptionInfo.cbMinEnterMember=m_pGameServiceOption->cbMinEnterMember;
	OptionCurrent.ServerOptionInfo.cbMaxEnterMember=m_pGameServiceOption->cbMaxEnterMember;

	//房间属性
	OptionCurrent.ServerOptionInfo.dwServerRule=m_pGameServiceOption->dwServerRule;
	lstrcpyn(OptionCurrent.ServerOptionInfo.szServerName,m_pGameServiceOption->szServerName,CountArray(OptionCurrent.ServerOptionInfo.szServerName));

	//聊天规则
	OptionCurrent.dwRuleMask|=SR_FORFEND_GAME_CHAT;
	OptionCurrent.dwRuleMask|=SR_FORFEND_ROOM_CHAT;
	OptionCurrent.dwRuleMask|=SR_FORFEND_WISPER_CHAT;
	OptionCurrent.dwRuleMask|=SR_FORFEND_WISPER_ON_GAME;

	//房间规则
	OptionCurrent.dwRuleMask|=SR_FORFEND_ROOM_ENTER;
	OptionCurrent.dwRuleMask|=SR_FORFEND_GAME_ENTER;
	OptionCurrent.dwRuleMask|=SR_FORFEND_GAME_LOOKON;

	//银行规则
	OptionCurrent.dwRuleMask|=SR_FORFEND_TAKE_IN_ROOM;
	OptionCurrent.dwRuleMask|=SR_FORFEND_TAKE_IN_GAME;
	OptionCurrent.dwRuleMask|=SR_FORFEND_SAVE_IN_ROOM;
	OptionCurrent.dwRuleMask|=SR_FORFEND_SAVE_IN_GAME;

	//其他规则
	//OptionCurrent.dwRuleMask|=SR_RECORD_GAME_TRACK;
	OptionCurrent.dwRuleMask|=SR_FORFEND_GAME_RULE;
	OptionCurrent.dwRuleMask|=SR_FORFEND_LOCK_TABLE;
	OptionCurrent.dwRuleMask|=SR_ALLOW_ANDROID_SIMULATE;

	//组件规则
	if (m_pGameServiceAttrib->cbDynamicJoin==TRUE) OptionCurrent.dwRuleMask|=SR_ALLOW_DYNAMIC_JOIN;
	if (m_pGameServiceAttrib->cbAndroidUser==TRUE) OptionCurrent.dwRuleMask|=SR_ALLOW_ANDROID_ATTEND;
	//if (m_pGameServiceAttrib->cbOffLineTrustee==TRUE) OptionCurrent.dwRuleMask|=SR_ALLOW_OFFLINE_TRUSTEE;

	//模式规则
	if ((m_pGameServiceOption->wServerType&(GAME_GENRE_GOLD|GAME_GENRE_MATCH))==0) OptionCurrent.dwRuleMask|=SR_RECORD_GAME_SCORE;
	if ((m_pGameServiceOption->wServerType&(GAME_GENRE_GOLD|GAME_GENRE_MATCH))==0) OptionCurrent.dwRuleMask|=SR_IMMEDIATE_WRITE_SCORE;

	//发送数据
	SendData(pIServerUserItem,MDM_GR_MANAGE,SUB_GR_OPTION_CURRENT,&OptionCurrent,sizeof(OptionCurrent));

	return true;
}

//房间设置
bool CAttemperEngineSink::OnTCPNetworkSubOptionServer(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_ServerOption));
	if (wDataSize!=sizeof(CMD_GR_ServerOption)) return false;

	//变量定义
	CMD_GR_ServerOption * pServerOption=(CMD_GR_ServerOption *)pData;
	tagServerOptionInfo * pServerOptionInfo=&pServerOption->ServerOptionInfo;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT((pIServerUserItem!=NULL)&&(pIServerUserItem->GetMasterOrder()>0));
	if ((pIServerUserItem==NULL)||(pIServerUserItem->GetMasterOrder()==0)) return false;

	//变量定义
	bool bModifyServer=false;

	//挂接节点
	if (m_pGameServiceOption->wNodeID!=pServerOptionInfo->wNodeID)
	{
		bModifyServer=true;
		m_pGameServiceOption->wNodeID=pServerOptionInfo->wNodeID;
	}

	//挂接类型
	if ((pServerOptionInfo->wKindID!=0)&&(m_pGameServiceOption->wKindID!=pServerOptionInfo->wKindID))
	{
		bModifyServer=true;
		m_pGameServiceOption->wKindID=pServerOptionInfo->wKindID;
	}
	
	//挂接排序
	if ((pServerOptionInfo->wSortID!=0)&&(m_pGameServiceOption->wSortID!=pServerOptionInfo->wSortID))
	{
		bModifyServer=true;
		m_pGameServiceOption->wSortID=pServerOptionInfo->wSortID;
	}

	//房间名字
	if ((pServerOptionInfo->szServerName[0]!=0)&&(lstrcmp(m_pGameServiceOption->szServerName,pServerOptionInfo->szServerName)!=0))
	{
		bModifyServer=true;
		lstrcpyn(m_pGameServiceOption->szServerName,pServerOptionInfo->szServerName,CountArray(m_pGameServiceOption->szServerName));
	}

#ifndef _READ_TABLE_OPTION_
	//税收配置
	m_pGameServiceOption->wRevenueRatio=pServerOptionInfo->wRevenueRatio;
	m_pGameServiceOption->lServiceScore=pServerOptionInfo->lServiceScore;

	//房间配置
	m_pGameServiceOption->lRestrictScore=pServerOptionInfo->lRestrictScore;
	m_pGameServiceOption->lMinTableScore=pServerOptionInfo->lMinTableScore;
#else
	// anjay !!
	// GM配置暂时失效,需要修改协议和客户端
#endif
	m_pGameServiceOption->lMinEnterScore=pServerOptionInfo->lMinEnterScore;
	m_pGameServiceOption->lMaxEnterScore=pServerOptionInfo->lMaxEnterScore;

	//会员限制
	m_pGameServiceOption->cbMinEnterMember=pServerOptionInfo->cbMinEnterMember;
	m_pGameServiceOption->cbMaxEnterMember=pServerOptionInfo->cbMaxEnterMember;

	//聊天规则
	CServerRule::SetForfendGameChat(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendGameChat(pServerOptionInfo->dwServerRule));
	CServerRule::SetForfendRoomChat(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendRoomChat(pServerOptionInfo->dwServerRule));
	CServerRule::SetForfendWisperChat(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendWisperChat(pServerOptionInfo->dwServerRule));
	CServerRule::SetForfendWisperOnGame(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendWisperOnGame(pServerOptionInfo->dwServerRule));

	//房间规则
	CServerRule::SetForfendRoomEnter(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendRoomEnter(pServerOptionInfo->dwServerRule));
	CServerRule::SetForfendGameEnter(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendGameEnter(pServerOptionInfo->dwServerRule));
	CServerRule::SetForfendGameLookon(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendGameLookon(pServerOptionInfo->dwServerRule));

	//银行规则
	CServerRule::SetForfendTakeInRoom(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendTakeInRoom(pServerOptionInfo->dwServerRule));
	CServerRule::SetForfendTakeInGame(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendTakeInGame(pServerOptionInfo->dwServerRule));
	CServerRule::SetForfendSaveInRoom(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendSaveInRoom(pServerOptionInfo->dwServerRule));
	CServerRule::SetForfendSaveInGame(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendSaveInGame(pServerOptionInfo->dwServerRule));

	//其他规则
	CServerRule::SetRecordGameTrack(m_pGameServiceOption->dwServerRule,CServerRule::IsRecordGameTrack(pServerOptionInfo->dwServerRule));
	CServerRule::SetForfendGameRule(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendGameRule(pServerOptionInfo->dwServerRule));
	CServerRule::SetForfendLockTable(m_pGameServiceOption->dwServerRule,CServerRule::IsForfendLockTable(pServerOptionInfo->dwServerRule));

	//动态加入
	if (m_pGameServiceAttrib->cbDynamicJoin==TRUE)
	{
		CServerRule::SetAllowDynamicJoin(m_pGameServiceOption->dwServerRule,CServerRule::IsAllowDynamicJoin(pServerOptionInfo->dwServerRule));
	}

	//机器管理
	if (m_pGameServiceAttrib->cbAndroidUser==TRUE)
	{
		CServerRule::SetAllowAndroidAttend(m_pGameServiceOption->dwServerRule,CServerRule::IsAllowAndroidAttend(pServerOptionInfo->dwServerRule));
	}

	//断线托管
	if (m_pGameServiceAttrib->cbOffLineTrustee==TRUE)
	{
		CServerRule::SetAllowOffLineTrustee(m_pGameServiceOption->dwServerRule,CServerRule::IsAllowOffLineTrustee(pServerOptionInfo->dwServerRule));
	}

	//记录游戏币
	if ((m_pGameServiceOption->wServerType&(GAME_GENRE_GOLD|GAME_GENRE_MATCH))==0)
	{
		CServerRule::SetRecordGameScore(m_pGameServiceOption->dwServerRule,CServerRule::IsRecordGameScore(pServerOptionInfo->dwServerRule));
	}

	//立即写分
	if ((m_pGameServiceOption->wServerType&(GAME_GENRE_GOLD|GAME_GENRE_MATCH))==0)
	{
		CServerRule::SetImmediateWriteScore(m_pGameServiceOption->dwServerRule,CServerRule::IsImmediateWriteScore(pServerOptionInfo->dwServerRule));
	}

	//调整参数
	CServiceUnits * pServiceUnits=CServiceUnits::g_pServiceUnits;
	pServiceUnits->RectifyServiceParameter();

	//发送修改
	if (bModifyServer==true)
	{
		//变量定义
		CMD_CS_C_ServerModify ServerModify;
		ZeroMemory(&ServerModify,sizeof(ServerModify));

		//服务端口
		ServerModify.wServerPort=pServiceUnits->m_TCPNetworkEngine->GetCurrentPort();

		//房间信息
		ServerModify.wKindID=m_pGameServiceOption->wKindID;
		ServerModify.wNodeID=m_pGameServiceOption->wNodeID;
		ServerModify.wSortID=m_pGameServiceOption->wSortID;
		ServerModify.dwOnLineCount=m_ServerUserManager.GetUserItemCount();
		ServerModify.dwFullCount=m_pGameServiceOption->wMaxPlayer-RESERVE_USER_COUNT;
		lstrcpyn(ServerModify.szServerName,m_pGameServiceOption->szServerName,CountArray(ServerModify.szServerName));
		lstrcpyn(ServerModify.szServerAddr,m_pInitParameter->m_ServiceAddress.szAddress,CountArray(ServerModify.szServerAddr));

		//发送数据
		m_pITCPSocketService->SendData(MDM_CS_SERVICE_INFO,SUB_CS_C_SERVER_MODIFY,&ServerModify,sizeof(ServerModify));
	}

	//发送信息
	SendRoomMessage(pIServerUserItem,TEXT("当前游戏服务器房间的“运行值”状态配置数据修改成功"),SMT_CHAT|SMT_EJECT);

	//输出信息
	TCHAR szBuffer[128]=TEXT("");
	_sntprintf(szBuffer,CountArray(szBuffer),TEXT("远程修改房间配置通知 管理员 %s [ %ld ]"),pIServerUserItem->GetNickName(),pIServerUserItem->GetUserID());

	//输出信息
	CTraceService::TraceString(szBuffer,TraceLevel_Info);
	LOGI("CAttemperEngineSink::OnTCPNetworkSubOptionServer, "<<szBuffer)

	return true;
}

//踢出用户
bool CAttemperEngineSink::OnTCPNetworkSubManagerKickUser(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize==sizeof(CMD_GR_KickUser));
	if (wDataSize!=sizeof(CMD_GR_KickUser)) return false;

	//变量定义
	CMD_GR_KickUser * pKickUser=(CMD_GR_KickUser *)pData;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);

	//用户效验
	ASSERT((pIServerUserItem!=NULL)&&(pIServerUserItem->GetMasterOrder()>0));
	if ((pIServerUserItem==NULL)||(pIServerUserItem->GetMasterOrder()==0)) return false;

	//权限判断
	ASSERT(CMasterRight::CanKillUser(pIServerUserItem->GetMasterRight())==true);
	if (CMasterRight::CanKillUser(pIServerUserItem->GetMasterRight())==false) return false;

	//目标用户
	IServerUserItem * pITargetUserItem = m_ServerUserManager.SearchUserItem(pKickUser->dwTargetUserID);
	if(pITargetUserItem==NULL) return true;

	//用户状态
	if(pITargetUserItem->GetUserStatus()==US_PLAYING) return true;

	//踢出记录
	m_KickUserItemMap[pITargetUserItem->GetUserID()]=(DWORD)time(NULL);

	//请离桌子
	WORD wTargerTableID = pITargetUserItem->GetTableID();
	if(wTargerTableID != INVALID_TABLE)
	{
		//发送消息
		SendGameMessage(pITargetUserItem,TEXT("你已被管理员请离桌子！"),SMT_CHAT|SMT_CLOSE_GAME);

		CTableFrame * pTableFrame=m_TableFrameArray[wTargerTableID];
		LOGI("CAttemperEngineSink::OnTCPNetworkSubManagerKickUser PerformStandUpAction, NickName="<<pITargetUserItem->GetNickName());
		if (pTableFrame->PerformStandUpAction(pITargetUserItem)==false) return true;
	}

	//发送通知
	LPCTSTR pszMessage=TEXT("你已被管理员请离此游戏房间！");
	SendRoomMessage(pITargetUserItem,pszMessage,SMT_CHAT|SMT_EJECT|SMT_GLOBAL|SMT_CLOSE_ROOM);

	pITargetUserItem->SetUserStatus(US_NULL,INVALID_TABLE,INVALID_CHAIR);

	return true;
}

//限制聊天
bool CAttemperEngineSink::OnTCPNetworkSubLimitUserChat(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验数据
	ASSERT(wDataSize==sizeof(CMD_GR_LimitUserChat));
	if (wDataSize!=sizeof(CMD_GR_LimitUserChat)) return false;

	//消息处理
	CMD_GR_LimitUserChat * pLimitUserChat=(CMD_GR_LimitUserChat *)pData;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);
	if (pIServerUserItem==NULL) return false;

	//权限判断
	//ASSERT(CMasterRight::CanLimitUserChat(pIServerUserItem->GetMasterRight())==true);
	//if (CMasterRight::CanLimitUserChat(pIServerUserItem->GetMasterRight())==false) return false;

	//目标用户
	IServerUserItem * pITargerUserItem=m_ServerUserManager.SearchUserItem(pLimitUserChat->dwTargetUserID);
	if (pITargerUserItem==NULL) return true;

	//变量定义
	DWORD dwAddRight = 0, dwRemoveRight = 0;

	//大厅聊天
	if (pLimitUserChat->cbLimitFlags==OSF_ROOM_CHAT)
	{
		if (CMasterRight::CanLimitRoomChat(pIServerUserItem->GetMasterRight())==false) return false;

		if( pLimitUserChat->cbLimitValue == TRUE )
			dwAddRight |= UR_CANNOT_ROOM_CHAT;
		else
			dwRemoveRight |= UR_CANNOT_ROOM_CHAT;
	}

	//游戏聊天
	if (pLimitUserChat->cbLimitFlags==OSF_GAME_CHAT)
	{
		if (CMasterRight::CanLimitGameChat(pIServerUserItem->GetMasterRight())==false) return false;

		if( pLimitUserChat->cbLimitValue == TRUE )
			dwAddRight |= UR_CANNOT_GAME_CHAT;
		else
			dwRemoveRight |= UR_CANNOT_GAME_CHAT;
	}

	//大厅私聊
	if (pLimitUserChat->cbLimitFlags==OSF_ROOM_WISPER)
	{
		if (CMasterRight::CanLimitWisper(pIServerUserItem->GetMasterRight())==false) return false;

		if( pLimitUserChat->cbLimitValue == TRUE )
			dwAddRight |= UR_CANNOT_WISPER;
		else
			dwRemoveRight |= UR_CANNOT_WISPER;
	}

	//发送喇叭
	if(pLimitUserChat->cbLimitFlags==OSF_SEND_BUGLE)
	{
		if (CMasterRight::CanLimitUserChat(pIServerUserItem->GetMasterRight())==false) return false;

		if(pLimitUserChat->cbLimitValue == TRUE)
			dwAddRight |= UR_CANNOT_BUGLE;
		else
			dwRemoveRight |= UR_CANNOT_BUGLE;
	}

	if( dwAddRight != 0 || dwRemoveRight != 0 )
	{
		pITargerUserItem->ModifyUserRight(dwAddRight,dwRemoveRight);

		//发送通知
		CMD_GR_ConfigUserRight cur = {0};
		cur.dwUserRight = pITargerUserItem->GetUserRight();

		SendData( pITargerUserItem,MDM_GR_CONFIG,SUB_GR_CONFIG_USER_RIGHT,&cur,sizeof(cur) );

		//发送消息
		SendRoomMessage(pIServerUserItem,TEXT("用户聊天权限配置成功！"),SMT_CHAT);
	}
	else return false;

	return true;
}

//踢出所有用户
bool CAttemperEngineSink::OnTCPNetworkSubKickAllUser(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//消息处理
	CMD_GR_KickAllUser * pKillAllUser=(CMD_GR_KickAllUser *)pData;

	//效验数据
	ASSERT(wDataSize<=sizeof(CMD_GR_KickAllUser));
	if( wDataSize > sizeof(CMD_GR_KickAllUser) ) return false;
	ASSERT(wDataSize==CountStringBuffer(pKillAllUser->szKickMessage));
	if (wDataSize!=CountStringBuffer(pKillAllUser->szKickMessage)) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);
	if (pIServerUserItem==NULL) return false;

	//权限判断
	ASSERT(CMasterRight::CanKillUser(pIServerUserItem->GetMasterRight())==true);
	if (CMasterRight::CanKillUser(pIServerUserItem->GetMasterRight())==false) return false;

	//解散所有游戏
	for (size_t i=0;i<m_TableFrameArray.size();i++)
	{
		//获取桌子
		CTableFrame * pTableFrame=m_TableFrameArray[i];
		if ( !pTableFrame->IsGameStarted() ) continue;

		pTableFrame->DismissGame();
	}

	tagBindParameter *pBindParameter = m_pNormalParameter;
	for( INT i = 0; i < m_pGameServiceOption->wMaxPlayer; i++ )
	{
		//目录用户
		IServerUserItem * pITargerUserItem= pBindParameter->pIServerUserItem;
		if (pITargerUserItem==NULL || pITargerUserItem==pIServerUserItem ) 
		{
			pBindParameter++;
			continue;
		}

		//发送消息
		SendRoomMessage(pITargerUserItem,pKillAllUser->szKickMessage,SMT_CHAT|SMT_EJECT|SMT_CLOSE_LINK|SMT_CLOSE_ROOM);

		pBindParameter++;
	} 

	return true;
}

//发布消息
bool CAttemperEngineSink::OnTCPNetworkSubSendMessage(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);
	if (pIServerUserItem==NULL) return false;

	//权限判断
	ASSERT(CMasterRight::CanIssueMessage(pIServerUserItem->GetMasterRight())==true);
	if (CMasterRight::CanIssueMessage(pIServerUserItem->GetMasterRight())==false) return false;

	//变量定义
	CMD_GR_SendMessage *pSysMessage = (CMD_GR_SendMessage *)pData;

	if(pSysMessage->cbLoop==TRUE)
	{
		//定于变量
		tagSystemMessage  *pSendMessage=new tagSystemMessage;
		ZeroMemory(pSendMessage, sizeof(tagSystemMessage));

		//构造消息
		pSendMessage->dwLastTime=(DWORD)time(NULL);
		pSendMessage->SystemMessage.cbMessageID=100;
		pSendMessage->SystemMessage.cbAllRoom=pSysMessage->cbAllRoom;
		if(pSysMessage->cbRoom==TRUE && pSysMessage->cbGame==TRUE)
			pSendMessage->SystemMessage.cbMessageType=3;
		else if(pSysMessage->cbRoom==TRUE)
			pSendMessage->SystemMessage.cbMessageType=2;
		else if(pSysMessage->cbGame==TRUE)
			pSendMessage->SystemMessage.cbMessageType=1;
		pSendMessage->SystemMessage.dwTimeRate=pSysMessage->dwTimeRate;
		pSendMessage->SystemMessage.tConcludeTime=pSysMessage->tConcludeTime;
		lstrcpyn(pSendMessage->SystemMessage.szSystemMessage, pSysMessage->szSystemMessage, CountArray(pSendMessage->SystemMessage.szSystemMessage));

		//记录消息
		m_SystemMessageList.AddTail(pSendMessage);
	}


	//消息处理
	return SendSystemMessage((CMD_GR_SendMessage *)pData, wDataSize);
}

//解散游戏
bool CAttemperEngineSink::OnTCPNetworkSubDismissGame(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验数据
	ASSERT(wDataSize==sizeof(CMD_GR_DismissGame));
	if (wDataSize!=sizeof(CMD_GR_DismissGame)) return false;

	//获取用户
	WORD wBindIndex=LOWORD(dwSocketID);
	IServerUserItem * pIServerUserItem=GetBindUserItem(wBindIndex);
	if (pIServerUserItem==NULL) return false;

	//权限判断
	ASSERT(CMasterRight::CanDismissGame(pIServerUserItem->GetMasterRight())==true);
	if (CMasterRight::CanDismissGame(pIServerUserItem->GetMasterRight())==false) return false;

	//消息处理
	CMD_GR_DismissGame * pDismissGame=(CMD_GR_DismissGame *)pData;

	//效验数据
	if(pDismissGame->wDismissTableNum >= m_TableFrameArray.size()) return true;

	//解散游戏
	CTableFrame *pTableFrame=m_TableFrameArray[pDismissGame->wDismissTableNum];
	if(pTableFrame)
	{
		if(pTableFrame->IsGameStarted()) pTableFrame->DismissGame();
	}

	return true;
}

// 自建桌子
bool CAttemperEngineSink::OnTCPSocketUserCreateCustomTable(VOID * pData, WORD wDataSize)
{
	//效验参数
	ASSERT(wDataSize == sizeof(CMD_CS_C_UserCustomTable));
	if (wDataSize != sizeof(CMD_CS_C_UserCustomTable)) return false;

	CMD_CS_C_UserCustomTable* pUserCustomTable = (CMD_CS_C_UserCustomTable*)pData;

	if (pUserCustomTable == nullptr)
	{
		return true;
	}

	// 判断是否还有空桌子可以用于自定义规则
	CTableFrame* pTableFrame = nullptr;
	for (size_t i = 0; i < m_TableFrameArray.size(); i++)
	{
		pTableFrame = m_TableFrameArray.at(i);
		if (pTableFrame == nullptr || pTableFrame->IsLockedForCustom() == true || pTableFrame->IsTableStarted() == true)
		{
			continue;
		}

		LOGI("CAttemperEngineSink::OnTCPSocketUserCreateCustomTable, 锁定桌子["<<i+1<<"], 准备创建自定义规则");
		// 锁定桌子准备创建
		pTableFrame->SetLockForCustom(true);

		// 数据库操作
		DBR_GR_UserCreateCustomTable DBUserCustomTable;
		DBUserCustomTable.dwUserID = pUserCustomTable->dwUserID;
		DBUserCustomTable.wCardCousumeCount = pUserCustomTable->wCardCousumeCount;
		DBUserCustomTable.wKindID = pUserCustomTable->wKindID;
		DBUserCustomTable.wServerID = m_pGameServiceOption->wServerID;
		DBUserCustomTable.wTableID = i;
		memcpy(DBUserCustomTable.cbCustomRule, pUserCustomTable->cbCustomRule, sizeof(DBUserCustomTable.cbCustomRule));

		m_pIDBCorrespondManager->PostDataBaseRequest(0L, DBR_GR_USER_CREATE_CUSTOMTABLE, pUserCustomTable->dwSocketID, &DBUserCustomTable, sizeof(DBR_GR_UserCreateCustomTable));

		return true;
	}

	// 没有空闲桌子,返回错误消息
	CMD_CS_C_UserCustomTableFailed UserCustomTableFailed;
	UserCustomTableFailed.lErrorCode = 1;
	lstrcpyn(UserCustomTableFailed.szDescribeString, TEXT("服务器满员,暂时无法创建房间,请稍后再试!如有疑问请与客服联系"), CountArray(UserCustomTableFailed.szDescribeString));
	UserCustomTableFailed.dwSocketID = pUserCustomTable->dwSocketID;
	m_pITCPSocketService->SendData(MDM_CS_REMOTE_SERVICE, SUB_CS_C_USER_CUSTOM_TABLE_FAILED, &UserCustomTableFailed, sizeof(UserCustomTableFailed));

	return true;
}

// 加入自建桌子
bool CAttemperEngineSink::OnTCPNetworkUserJoinCustomTable(VOID * pData, WORD wDataSize, DWORD dwSocketID)
{
	//效验参数
	ASSERT(wDataSize >= sizeof(CMD_MB_JoinCustomTable));
	if (wDataSize < sizeof(CMD_MB_JoinCustomTable)) return false;

	//处理消息
	CMD_MB_JoinCustomTable * pJoinCustomTable = (CMD_MB_JoinCustomTable *)pData;

	// 去数据库查询该玩家是第一次加入,还是之前已经加入过了,需要安排到原来的位置
	DBR_GR_UserJoinCustomTable UserJoinCustomTable;
	UserJoinCustomTable.dwUserID = pJoinCustomTable->dwUserID;
	lstrcpyn(UserJoinCustomTable.szSecret, pJoinCustomTable->szSecret, sizeof(UserJoinCustomTable.szSecret));

	m_pIDBCorrespondManager->PostDataBaseRequest(pJoinCustomTable->dwUserID, DBR_GR_USER_JOIN_CUSTOMTABLE, dwSocketID, &UserJoinCustomTable, sizeof(DBR_GR_UserJoinCustomTable));

	return true;
}

//用户登录
VOID CAttemperEngineSink::OnEventUserLogon(IServerUserItem * pIServerUserItem, bool bAlreadyOnLine)
{
	//获取参数
	WORD wBindIndex=pIServerUserItem->GetBindIndex();
	bool bAndroidUser=pIServerUserItem->IsAndroidUser();
	tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);

	//登录处理(PC流程和手机处理流程一样)
	//变量定义
	BYTE cbDataBuffer[SOCKET_TCP_PACKET];
	CMD_GR_LogonSuccess LogonSuccess;
	CMD_GR_ConfigServer* pConfigServer = (CMD_GR_ConfigServer *)cbDataBuffer;
	ZeroMemory(&LogonSuccess,sizeof(LogonSuccess));

	WORD wHeadSize = sizeof(CMD_GR_ConfigServer);
	CSendPacketHelper SendPacket(cbDataBuffer + wHeadSize, sizeof(cbDataBuffer) - wHeadSize);
	ZeroMemory(pConfigServer, sizeof(CMD_GR_ConfigServer));

	//登录成功
	LogonSuccess.dwUserRight=pIServerUserItem->GetUserRight();
	LogonSuccess.dwMasterRight=pIServerUserItem->GetMasterRight();
	SendData(pBindParameter->dwSocketID,MDM_GR_LOGON,SUB_GR_LOGON_SUCCESS,&LogonSuccess,sizeof(LogonSuccess));

	//房间配置
	pConfigServer->wTableCount = m_pGameServiceOption->wTableCount;
	pConfigServer->wChairCount = m_pGameServiceAttrib->wChairCount;
	pConfigServer->wServerType = m_pGameServiceOption->wServerType;
	pConfigServer->dwServerRule = m_pGameServiceOption->dwServerRule;

	// 附加房间配置
	if ( (m_pGameServiceOption->wServerType&GAME_GENRE_USERCUSTOM) == 0)
	{
		// 自建桌子不下发桌子信息
		DTP_GR_TableInfo DTPTableInfo;
		ZeroMemory(&DTPTableInfo, sizeof(DTPTableInfo));
		for (int i = 0; i < m_pGameServiceOption->wTableCount; i++)
		{
			int j = CountArray(DTPTableInfo.szTableName);
			if (i >= CountArray(DTPTableInfo.szTableName))
			{
				continue;
			}
			_tcsnccpy(DTPTableInfo.szTableName[i], m_pGameServiceTableOption->TableOption[i].szTableName, LEN_TABLENAME);
			DTPTableInfo.cTableNormalPicID[i] = (unsigned char)m_pGameServiceTableOption->TableOption[i].nNormalPicID;
			DTPTableInfo.cTablePlayingPicID[i] = (unsigned char)m_pGameServiceTableOption->TableOption[i].nPlayingPicID;
		}
		SendPacket.AddPacket(&DTPTableInfo, sizeof(DTPTableInfo), DTP_GR_TABLE_INFO);
	}

	WORD wSendSize = SendPacket.GetDataSize() + sizeof(CMD_GR_ConfigServer);
	SendData(pBindParameter->dwSocketID, MDM_GR_CONFIG, SUB_GR_CONFIG_SERVER, &cbDataBuffer, wSendSize);

	//列表配置
	WORD wConfigColumnHead=sizeof(m_DataConfigColumn)-sizeof(m_DataConfigColumn.ColumnItem);
	WORD wConfigColumnInfo=m_DataConfigColumn.cbColumnCount*sizeof(m_DataConfigColumn.ColumnItem[0]);
	SendData(pBindParameter->dwSocketID,MDM_GR_CONFIG,SUB_GR_CONFIG_COLUMN,&m_DataConfigColumn,wConfigColumnHead+wConfigColumnInfo);

	//道具配置
	WORD wConfigPropertyHead=sizeof(m_DataConfigProperty)-sizeof(m_DataConfigProperty.PropertyInfo);
	WORD wConfigPropertyInfo=m_DataConfigProperty.cbPropertyCount*sizeof(m_DataConfigProperty.PropertyInfo[0]);
	SendData(pBindParameter->dwSocketID,MDM_GR_CONFIG,SUB_GR_CONFIG_PROPERTY,&m_DataConfigProperty,wConfigPropertyHead+wConfigPropertyInfo);

	//配置完成
	SendData(pBindParameter->dwSocketID,MDM_GR_CONFIG,SUB_GR_CONFIG_FINISH,NULL,0);

	//玩家数据
	SendUserInfoPacket(pIServerUserItem,pBindParameter->dwSocketID);

	//在线用户
	if (m_pGameServiceOption->wServerType&GAME_GENRE_USERCUSTOM)
	{
		// 假如此时正在座位上,下发同桌玩家信息
		if (pIServerUserItem->GetTableID() != INVALID_TABLE )
		{
			WORD wUserIndex = 0;
			IServerUserItem * pIServerUserItemSend = NULL;
			while (true)
			{
				pIServerUserItemSend = m_ServerUserManager.EnumUserItem(wUserIndex++);
				if (pIServerUserItemSend == nullptr)
				{
					break;
				}
				if (pIServerUserItemSend == pIServerUserItem)
				{
					continue;
				}
				if (pIServerUserItemSend->GetTableID() == pIServerUserItem->GetTableID())
				{
					SendUserInfoPacket(pIServerUserItemSend, pBindParameter->dwSocketID);
				}
			}
		}
	}
	else
	{
		// 自建桌子类型,不下发房间里所有玩家的信息
		WORD wUserIndex = 0;
		IServerUserItem * pIServerUserItemSend = NULL;
		while (true)
		{
			pIServerUserItemSend = m_ServerUserManager.EnumUserItem(wUserIndex++);
			if (pIServerUserItemSend == NULL) break;
			if (pIServerUserItemSend == pIServerUserItem) continue;
			SendUserInfoPacket(pIServerUserItemSend, pBindParameter->dwSocketID);
		}
	}

	//桌子状态
	if ((m_pGameServiceOption->wServerType&GAME_GENRE_USERCUSTOM) == 0)
	{
		// 自建桌子类型,不下发房间里所有的桌子状态
		CMD_GR_TableInfo TableInfo;
		TableInfo.wTableCount = (WORD)m_TableFrameArray.size();
		ASSERT(TableInfo.wTableCount < CountArray(TableInfo.TableStatusArray));
		for (WORD i = 0; i < TableInfo.wTableCount; i++)
		{
			CTableFrame * pTableFrame = m_TableFrameArray[i];
			TableInfo.TableStatusArray[i].cbTableLock = pTableFrame->IsTableLocked() ? TRUE : FALSE;
			TableInfo.TableStatusArray[i].cbPlayStatus = pTableFrame->IsTableStarted() ? TRUE : FALSE;
		}

		//桌子状态
		wHeadSize = sizeof(TableInfo) - sizeof(TableInfo.TableStatusArray);
		wSendSize = wHeadSize + TableInfo.wTableCount*sizeof(TableInfo.TableStatusArray[0]);
		SendData(pBindParameter->dwSocketID, MDM_GR_STATUS, SUB_GR_TABLE_INFO, &TableInfo, wSendSize);
	}

	//第一次进入,发送通知,客户端添加改玩家信息
	if (bAlreadyOnLine==false)
	{
		if (m_pGameServiceOption->wServerType&GAME_GENRE_USERCUSTOM)
		{
			// 自己信息发送给同桌所有玩家
			CTableFrame* pTableFrame = nullptr;
			if (pIServerUserItem->GetTableID() < m_TableFrameArray.size())
			{
				pTableFrame = m_TableFrameArray.at(pIServerUserItem->GetTableID());
			}

			if (pTableFrame)
			{
				// 发送同桌信息
				for (WORD j = 0; j < pTableFrame->GetChairCount(); j++)
				{
					IServerUserItem* pTableUserItem = pTableFrame->GetTableUserItem(j);
					if (pTableUserItem == nullptr || pTableUserItem == pIServerUserItem)
					{
						continue;
					}

					// 自己的信息发送给同桌
					tagBindParameter * pBindParameter = GetBindParameter(pTableUserItem->GetBindIndex());
					if (pBindParameter != nullptr)
					{
						SendUserInfoPacket(pIServerUserItem, pBindParameter->dwSocketID);
					}
				}
			}
		}
		else
		{
			// 自己信息发送给房间所有玩家
			SendUserInfoPacket(pIServerUserItem, INVALID_DWORD);
		}
	}

	//登录完成
	SendData(pBindParameter->dwSocketID,MDM_GR_LOGON,SUB_GR_LOGON_FINISH,NULL,0);

	//欢迎消息
	if (bAndroidUser==false)
	{
		//构造提示
		TCHAR szMessage[128]=TEXT("");
		_sntprintf(szMessage,CountArray(szMessage),TEXT("欢迎您进入“%s-%s”游戏房间，祝您游戏愉快！"),m_pGameServiceAttrib->szGameName,m_pGameServiceOption->szServerName);

		//发送消息
		SendRoomMessage(pIServerUserItem,szMessage,SMT_CHAT);
	}

	//网络设置
	if (bAndroidUser==false)
	{
		if (pBindParameter->cbClientKind==CLIENT_KIND_MOBILE)
		{
			m_pITCPNetworkEngine->AllowBatchSend(pBindParameter->dwSocketID,true,BG_MOBILE);
		}
		else
		{
			m_pITCPNetworkEngine->AllowBatchSend(pBindParameter->dwSocketID,true,BG_COMPUTER);
		}
	}

	if(m_pIGameMatchServiceManager!=NULL)
	{
		m_pIGameMatchServiceManager->SendMatchInfo(pIServerUserItem);
	}
	return;
}

//用户离开
VOID CAttemperEngineSink::OnEventUserLogout(IServerUserItem * pIServerUserItem, DWORD dwLeaveReason)
{
	//变量定义
	DBR_GR_LeaveGameServer LeaveGameServer;
	ZeroMemory(&LeaveGameServer,sizeof(LeaveGameServer));

	//提取游戏币
	pIServerUserItem->QueryRecordInfo(LeaveGameServer.RecordInfo);
	pIServerUserItem->DistillVariation(LeaveGameServer.VariationInfo);

    LeaveGameServer.VariationInfo.dwDrawCount=0;
    LeaveGameServer.VariationInfo.dwExperience=0;
    LeaveGameServer.VariationInfo.dwFleeCount=0;
    LeaveGameServer.VariationInfo.dwLostCount=0;
    LeaveGameServer.VariationInfo.dwWinCount=0;

	//用户信息
	LeaveGameServer.dwLeaveReason=dwLeaveReason;
	LeaveGameServer.dwUserID=pIServerUserItem->GetUserID();
	LeaveGameServer.dwInoutIndex=pIServerUserItem->GetInoutIndex();
	LeaveGameServer.dwOnLineTimeCount=(DWORD)(time(NULL))-pIServerUserItem->GetLogonTime();

	//连接信息
	LeaveGameServer.dwClientAddr=pIServerUserItem->GetClientAddr();
	lstrcpyn(LeaveGameServer.szMachineID,pIServerUserItem->GetMachineID(),CountArray(LeaveGameServer.szMachineID));

	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(pIServerUserItem->GetUserID(),DBR_GR_LEAVE_GAME_SERVER,0L,&LeaveGameServer,sizeof(LeaveGameServer), TRUE);
	
	//汇总用户
	if (m_bCollectUser==true)
	{
		//变量定义
		CMD_CS_C_UserLeave UserLeave;
		ZeroMemory(&UserLeave,sizeof(UserLeave));

		//设置变量
		UserLeave.dwUserID=pIServerUserItem->GetUserID();

		//发送消息
		m_pITCPSocketService->SendData(MDM_CS_USER_COLLECT,SUB_CS_C_USER_LEAVE,&UserLeave,sizeof(UserLeave));
	}

	//知道比赛服务退出游戏
	if(m_pIGameMatchServiceManager!=NULL)m_pIGameMatchServiceManager->OnUserQuitGame(pIServerUserItem, 0);

	//分组删除
	DeleteWaitDistribute(pIServerUserItem);

	//删除用户
	m_ServerUserManager.DeleteUserItem(pIServerUserItem);

	return;
}

//解锁游戏币
bool CAttemperEngineSink::PerformUnlockScore(DWORD dwUserID, DWORD dwInoutIndex, DWORD dwLeaveReason)
{
	//变量定义
	DBR_GR_LeaveGameServer LeaveGameServer;
	ZeroMemory(&LeaveGameServer,sizeof(LeaveGameServer));

	//设置变量
	LeaveGameServer.dwUserID=dwUserID;
	LeaveGameServer.dwInoutIndex=dwInoutIndex;
	LeaveGameServer.dwLeaveReason=dwLeaveReason;

	//投递请求
	m_pIDBCorrespondManager->PostDataBaseRequest(dwUserID,DBR_GR_LEAVE_GAME_SERVER,0L,&LeaveGameServer,sizeof(LeaveGameServer));

	return true;
}

//版本检查
bool CAttemperEngineSink::PerformCheckVersion(DWORD dwPlazaVersion, DWORD dwFrameVersion, DWORD dwClientVersion, DWORD dwSocketID, bool bMobileClient/* = false*/)
{
	//变量定义
	bool bMustUpdateClient=false;
	bool bAdviceUpdateClient=false;


	//游戏版本
	if (bMobileClient == false)
	{
		if (VERSION_EFFICACY == TRUE)
		{
			if (GetSubVer(dwClientVersion) < GetSubVer(m_pGameServiceAttrib->dwClientVersion)) bAdviceUpdateClient = true;
			if (GetMainVer(dwClientVersion) != GetMainVer(m_pGameServiceAttrib->dwClientVersion)) bMustUpdateClient = true;
			if (GetProductVer(dwClientVersion) != GetProductVer(m_pGameServiceAttrib->dwClientVersion)) bMustUpdateClient = true;
		}
		else
		{
			if (GetSubVer(dwClientVersion) < GetSubVer(m_pGameParameter->dwClientVersion)) bAdviceUpdateClient = true;
			if (GetMainVer(dwClientVersion) != GetMainVer(m_pGameParameter->dwClientVersion)) bMustUpdateClient = true;
			if (GetProductVer(dwClientVersion) != GetProductVer(m_pGameParameter->dwClientVersion)) bMustUpdateClient = true;
		}
	}
	else
	{
// 		if (VERSION_EFFICACY == TRUE)
// 		{
// 			if (GetSubVer(dwClientVersion) < GetSubVer(m_pGameServiceAttrib->dwAppClientVersion)) bAdviceUpdateClient = true;
// 			if (GetMainVer(dwClientVersion) != GetMainVer(m_pGameServiceAttrib->dwAppClientVersion)) bMustUpdateClient = true;
// 			if (GetProductVer(dwClientVersion) != GetProductVer(m_pGameServiceAttrib->dwAppClientVersion)) bMustUpdateClient = true;
// 		}
// 		else
// 		{
// 			if (GetSubVer(dwClientVersion) < GetSubVer(m_pGameParameter->dwAppClientVersion)) bAdviceUpdateClient = true;
//  			if (GetMainVer(dwClientVersion) != GetMainVer(m_pGameParameter->dwAppClientVersion)) bMustUpdateClient = true;
//  			if (GetProductVer(dwClientVersion) != GetProductVer(m_pGameParameter->dwAppClientVersion)) bMustUpdateClient = true;
// 		}
	}


	//更新通知
	if ((bMustUpdateClient==true)||(bAdviceUpdateClient==true))
	{
		//变量定义
		CMD_GR_UpdateNotify UpdateNotify;
		ZeroMemory(&UpdateNotify,sizeof(UpdateNotify));

		//变量定义
		UpdateNotify.cbMustUpdatePlaza=false;
		UpdateNotify.cbMustUpdateClient=bMustUpdateClient;
		UpdateNotify.cbAdviceUpdateClient=bAdviceUpdateClient;

		//当前版本
		UpdateNotify.dwCurrentPlazaVersion=VERSION_PLAZA;
		UpdateNotify.dwCurrentFrameVersion=VERSION_FRAME;
		if (bMobileClient == false)
		{
			UpdateNotify.dwCurrentClientVersion = m_pGameServiceAttrib->dwClientVersion;
		}
		else
		{
//			UpdateNotify.dwCurrentClientVersion = m_pGameServiceAttrib->dwAppClientVersion;
		}

		//发送消息
		SendData(dwSocketID,MDM_GR_LOGON,SUB_GR_UPDATE_NOTIFY,&UpdateNotify,sizeof(UpdateNotify));

		//中止判断
		if (bMustUpdateClient==true)
		{
//			m_pITCPNetworkEngine->ShutDownSocket(dwSocketID);
			return false;
		}
	}

	return true;
}

//切换连接
bool CAttemperEngineSink::SwitchUserItemConnect(IServerUserItem * pIServerUserItem, TCHAR szMachineID[LEN_MACHINE_ID], WORD wTargetIndex,BYTE cbDeviceType,WORD wBehaviorFlags,WORD wPageTableCount)
{
	//效验参数
	ASSERT((pIServerUserItem!=NULL)&&(wTargetIndex!=INVALID_WORD));
	if ((pIServerUserItem==NULL)||(wTargetIndex==INVALID_WORD)) return false;

	//断开用户
	if (pIServerUserItem->GetBindIndex()!=INVALID_WORD)
	{
		//发送通知
		LPCTSTR pszMessage=TEXT("请注意，您的帐号在另一地方进入了此游戏房间，您被迫离开！");
		SendRoomMessage(pIServerUserItem,pszMessage,SMT_CHAT|SMT_EJECT|SMT_GLOBAL|SMT_CLOSE_ROOM);

		//绑定参数
		WORD wSourceIndex=pIServerUserItem->GetBindIndex();
		tagBindParameter * pSourceParameter=GetBindParameter(wSourceIndex);

		//解除绑定
		ASSERT((pSourceParameter!=NULL)&&(pSourceParameter->pIServerUserItem==pIServerUserItem));
		if ((pSourceParameter!=NULL)&&(pSourceParameter->pIServerUserItem==pIServerUserItem)) pSourceParameter->pIServerUserItem=NULL;

		//断开用户
		if (pIServerUserItem->IsAndroidUser()==true)
		{
			LOGI("CAttemperEngineSink::SwitchUserItemConnect DeleteAndroidUserItem, wAndroidIndex="<<LOWORD(pSourceParameter->dwSocketID)<<", wRoundID="<<HIWORD(pSourceParameter->dwSocketID));
			m_AndroidUserManager.DeleteAndroidUserItem(pSourceParameter->dwSocketID);
		}
		else
		{
			m_pITCPNetworkEngine->ShutDownSocket(pSourceParameter->dwSocketID);
		}
	}

	//状态切换
	bool bIsOffLine=false;
	if (pIServerUserItem->GetUserStatus()==US_OFFLINE)
	{
		//变量定义
		WORD wTableID=pIServerUserItem->GetTableID();
		WORD wChairID=pIServerUserItem->GetChairID();

		//设置状态
		bIsOffLine=true;
		pIServerUserItem->SetUserStatus(US_PLAYING,wTableID,wChairID);
	}
	if (pIServerUserItem->GetUserStatus() == US_TRUSTEE && (m_pGameServiceOption->wServerType&GAME_GENRE_USERCUSTOM))
	{
		//变量定义
		WORD wTableID = pIServerUserItem->GetTableID();
		WORD wChairID = pIServerUserItem->GetChairID();

		//设置状态
		bIsOffLine = true;
		pIServerUserItem->SetUserStatus(US_PLAYING, wTableID, wChairID);
	}

	//机器判断
	LPCTSTR pszMachineID=pIServerUserItem->GetMachineID();
	bool bSameMachineID=(lstrcmp(pszMachineID,szMachineID)==0);

	//变量定义
	bool bAndroidUser=(wTargetIndex>=INDEX_ANDROID);
	tagBindParameter * pTargetParameter=GetBindParameter(wTargetIndex);

	//激活用户
	pTargetParameter->pIServerUserItem=pIServerUserItem;
	pIServerUserItem->SetUserParameter(pTargetParameter->dwClientAddr,wTargetIndex,szMachineID,bAndroidUser,false);

	//手机标识
	if(pTargetParameter->cbClientKind==CLIENT_KIND_MOBILE)
	{
		pIServerUserItem->SetMobileUser(true);
		SetMobileUserParameter(pIServerUserItem,cbDeviceType,wBehaviorFlags,wPageTableCount);
	}

	//登录事件
	OnEventUserLogon(pIServerUserItem,true);

	//安全提示
	if ((bAndroidUser==false)&&(bIsOffLine==false)&&(bSameMachineID==false))
	{
		SendRoomMessage(pIServerUserItem,TEXT("请注意，您的帐号在另一地方进入了此游戏房间，对方被迫离开！"),SMT_EJECT|SMT_CHAT|SMT_GLOBAL);
	}

	if (m_pGameServiceOption->wServerType&GAME_GENRE_USERCUSTOM)
	{
		//变量定义
		WORD wTableID = pIServerUserItem->GetTableID();
		WORD wChairID = pIServerUserItem->GetChairID();
		CTableFrame* pTableFrame = nullptr;
		if (wTableID < m_TableFrameArray.size())
		{
			pTableFrame = m_TableFrameArray.at(wTableID);
		}

		if (pTableFrame)
		{
			// 发送同桌信息
			for (WORD j = 0; j < pTableFrame->GetChairCount(); j++)
			{
 				IServerUserItem* pTableUserItem = pTableFrame->GetTableUserItem(j);
 				if (pTableUserItem == nullptr || pTableUserItem == pIServerUserItem)
 				{
 					continue;
 				}
 
 				// 同桌的信息发送给自己
				SendUserInfoPacket(pTableUserItem, pTargetParameter->dwSocketID);

				// 自己的信息发送给同桌
				tagBindParameter * pBindParameter = GetBindParameter(pTableUserItem->GetBindIndex());
				if (pBindParameter != nullptr)
				{
					SendUserInfoPacket(pIServerUserItem, pBindParameter->dwSocketID);
				}
			}
		}
	}

	return true;
}

//登录失败
bool CAttemperEngineSink::SendLogonFailure(LPCTSTR pszString, LONG lErrorCode, DWORD dwSocketID)
{
	//变量定义
	CMD_GR_LogonFailure LogonFailure;
	ZeroMemory(&LogonFailure,sizeof(LogonFailure));

	//构造数据
	LogonFailure.lErrorCode=lErrorCode;
	lstrcpyn(LogonFailure.szDescribeString,pszString,CountArray(LogonFailure.szDescribeString));

	//数据属性
	WORD wDataSize=CountStringBuffer(LogonFailure.szDescribeString);
	WORD wHeadSize=sizeof(LogonFailure)-sizeof(LogonFailure.szDescribeString);

	//发送数据
	SendData(dwSocketID,MDM_GR_LOGON,SUB_GR_LOGON_FAILURE,&LogonFailure,wHeadSize+wDataSize);

	return true;
}

//银行失败
bool CAttemperEngineSink::SendInsureFailure(IServerUserItem * pIServerUserItem, LPCTSTR pszString, LONG lErrorCode,BYTE cbActivityGame)
{
	//效验参数
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//变量定义
	CMD_GR_S_UserInsureFailure UserInsureFailure;
	ZeroMemory(&UserInsureFailure,sizeof(UserInsureFailure));

	//构造数据
	UserInsureFailure.cbActivityGame=cbActivityGame;
	UserInsureFailure.lErrorCode=lErrorCode;
	lstrcpyn(UserInsureFailure.szDescribeString,pszString,CountArray(UserInsureFailure.szDescribeString));

	//数据属性
	WORD wDataSize=CountStringBuffer(UserInsureFailure.szDescribeString);
	WORD wHeadSize=sizeof(UserInsureFailure)-sizeof(UserInsureFailure.szDescribeString);

	//发送数据
	SendData(pIServerUserItem,MDM_GR_INSURE,SUB_GR_USER_INSURE_FAILURE,&UserInsureFailure,wHeadSize+wDataSize);

	return true;
}

//请求失败
bool CAttemperEngineSink::SendRequestFailure(IServerUserItem * pIServerUserItem, LPCTSTR pszDescribe, LONG lErrorCode)
{
	//变量定义
	CMD_GR_RequestFailure RequestFailure;
	ZeroMemory(&RequestFailure,sizeof(RequestFailure));

	//设置变量
	RequestFailure.lErrorCode=lErrorCode;
	lstrcpyn(RequestFailure.szDescribeString,pszDescribe,CountArray(RequestFailure.szDescribeString));

	//发送数据
	WORD wDataSize=CountStringBuffer(RequestFailure.szDescribeString);
	WORD wHeadSize=sizeof(RequestFailure)-sizeof(RequestFailure.szDescribeString);
	SendData(pIServerUserItem,MDM_GR_USER,SUB_GR_REQUEST_FAILURE,&RequestFailure,wHeadSize+wDataSize);

	return true;
}

//道具失败
bool CAttemperEngineSink::SendPropertyFailure(IServerUserItem * pIServerUserItem, LPCTSTR pszDescribe, LONG lErrorCode,WORD wRequestArea)
{
	//变量定义
	CMD_GR_PropertyFailure PropertyFailure;
	ZeroMemory(&PropertyFailure,sizeof(PropertyFailure));

	//设置变量
	PropertyFailure.lErrorCode=lErrorCode;
	PropertyFailure.wRequestArea=wRequestArea;
	lstrcpyn(PropertyFailure.szDescribeString,pszDescribe,CountArray(PropertyFailure.szDescribeString));

	//发送数据
	WORD wDataSize=CountStringBuffer(PropertyFailure.szDescribeString);
	WORD wHeadSize=sizeof(PropertyFailure)-sizeof(PropertyFailure.szDescribeString);
	SendData(pIServerUserItem,MDM_GR_USER,SUB_GR_PROPERTY_FAILURE,&PropertyFailure,wHeadSize+wDataSize);

	return true;
}

//发送用户
bool CAttemperEngineSink::SendUserInfoPacket(IServerUserItem * pIServerUserItem, DWORD dwSocketID)
{
	//效验参数
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//变量定义
	BYTE cbBuffer[SOCKET_TCP_PACKET];
	tagUserInfo * pUserInfo=pIServerUserItem->GetUserInfo();
	tagUserInfoHead * pUserInfoHead=(tagUserInfoHead *)cbBuffer;
	CSendPacketHelper SendPacket(cbBuffer+sizeof(tagUserInfoHead),sizeof(cbBuffer)-sizeof(tagUserInfoHead));

	//用户属性
	pUserInfoHead->wFaceID=pUserInfo->wFaceID;
	pUserInfoHead->dwGameID=pUserInfo->dwGameID;
	pUserInfoHead->dwUserID=pUserInfo->dwUserID;
	pUserInfoHead->dwGroupID=pUserInfo->dwGroupID;
	pUserInfoHead->dwCustomID=pUserInfo->dwCustomID;

	//用户属性
	pUserInfoHead->cbGender=pUserInfo->cbGender;
	pUserInfoHead->cbMemberOrder=pUserInfo->cbMemberOrder;
	pUserInfoHead->cbMasterOrder=pUserInfo->cbMasterOrder;

	//用户状态
	pUserInfoHead->wTableID=pUserInfo->wTableID;
	pUserInfoHead->wChairID=pUserInfo->wChairID;
	pUserInfoHead->cbUserStatus=pUserInfo->cbUserStatus;

	//用户局数
	pUserInfoHead->dwWinCount=pUserInfo->dwWinCount;
	pUserInfoHead->dwLostCount=pUserInfo->dwLostCount;
	pUserInfoHead->dwDrawCount=pUserInfo->dwDrawCount;
	pUserInfoHead->dwFleeCount=pUserInfo->dwFleeCount;
	pUserInfoHead->dwUserMedal=pUserInfo->dwUserMedal;
	pUserInfoHead->dwExperience=pUserInfo->dwExperience;
	pUserInfoHead->lLoveLiness=pUserInfo->lLoveLiness;

	//用户积分
	pUserInfoHead->lGrade=pUserInfo->lGrade;
	pUserInfoHead->lInsure=pUserInfo->lInsure;

	//用户游戏币
	if (m_pGameServiceOption->wServerType & GAME_GENRE_USERCUSTOM)
	{
		pUserInfoHead->lScore = pUserInfo->lScore - CUSTOMTABLE_INIT_SCORE;
	}
	else
	{
		pUserInfoHead->lScore = pUserInfo->lScore;
	}
	pUserInfoHead->lScore+=pIServerUserItem->GetTrusteeScore();
	pUserInfoHead->lScore+=pIServerUserItem->GetFrozenedScore();

	//叠加信息
	SendPacket.AddPacket(pUserInfo->szNickName,DTP_GR_NICK_NAME);
	SendPacket.AddPacket(pUserInfo->szGroupName,DTP_GR_GROUP_NAME);
	SendPacket.AddPacket(pUserInfo->szUnderWrite,DTP_GR_UNDER_WRITE);

	//发送数据
	if (dwSocketID==INVALID_DWORD)
	{
		WORD wHeadSize=sizeof(tagUserInfoHead);
		SendData(BG_COMPUTER,MDM_GR_USER,SUB_GR_USER_ENTER,cbBuffer,wHeadSize+SendPacket.GetDataSize());
	}
	else
	{
		WORD wHeadSize=sizeof(tagUserInfoHead);
		SendData(dwSocketID,MDM_GR_USER,SUB_GR_USER_ENTER,cbBuffer,wHeadSize+SendPacket.GetDataSize());
	}

	return true;
}

//广播道具
bool CAttemperEngineSink::SendPropertyMessage(DWORD dwSourceID,DWORD dwTargerID,WORD wPropertyIndex,WORD wPropertyCount)
{
	//构造结构
	CMD_GR_S_PropertyMessage  PropertyMessage;
	PropertyMessage.wPropertyIndex=wPropertyIndex;
	PropertyMessage.dwSourceUserID=dwSourceID;
	PropertyMessage.dwTargerUserID=dwTargerID;
	PropertyMessage.wPropertyCount=wPropertyCount;

	//在线用户
	WORD wUserIndex=0;
	IServerUserItem * pIServerUserItemSend=NULL;
	while (true)
	{
		pIServerUserItemSend=m_ServerUserManager.EnumUserItem(wUserIndex++);
		if (pIServerUserItemSend==NULL) break;
		SendData(pIServerUserItemSend,MDM_GR_USER,SUB_GR_PROPERTY_MESSAGE,&PropertyMessage,sizeof(PropertyMessage));
	}

	return true;
}

//道具效应
bool CAttemperEngineSink::SendPropertyEffect(IServerUserItem * pIServerUserItem)
{
	//参数校验
	if(pIServerUserItem==NULL) return false;

	//构造结构
	CMD_GR_S_PropertyEffect  PropertyEffect;
	PropertyEffect.wUserID =pIServerUserItem->GetUserID();
	PropertyEffect.cbMemberOrder=pIServerUserItem->GetMemberOrder();

	//在线用户
	WORD wUserIndex=0;
	IServerUserItem * pIServerUserItemSend=NULL;
	while (true)
	{
		pIServerUserItemSend=m_ServerUserManager.EnumUserItem(wUserIndex++);
		if (pIServerUserItemSend==NULL) break;
		SendData(pIServerUserItemSend,MDM_GR_USER,SUB_GR_PROPERTY_EFFECT,&PropertyEffect,sizeof(PropertyEffect));
	}

	return true;
}

//道具事件
bool CAttemperEngineSink::OnEventPropertyBuyPrep(WORD cbRequestArea,WORD wPropertyIndex,IServerUserItem *pISourceUserItem,IServerUserItem *pTargetUserItem)
{
	//目标玩家
	if ( pTargetUserItem == NULL )
	{
		//发送消息
		SendPropertyFailure(pISourceUserItem,TEXT("赠送失败，您要赠送的玩家已经离开！"), 0L,cbRequestArea);

		return false;
	}

	//房间判断
	if ( (m_pGameServiceOption->wServerType == GAME_GENRE_GOLD || m_pGameServiceOption->wServerType == GAME_GENRE_MATCH ) && 
		(wPropertyIndex== PROPERTY_ID_SCORE_CLEAR||wPropertyIndex==PROPERTY_ID_TWO_CARD||wPropertyIndex == PROPERTY_ID_FOUR_CARD||wPropertyIndex == PROPERTY_ID_POSSESS) )
	{
		//发送消息
		SendPropertyFailure(pISourceUserItem,TEXT("此房间不可以使用此道具,购买失败"), 0L,cbRequestArea);

		return false;
	}

	//查找道具
	tagPropertyInfo * pPropertyInfo=m_GamePropertyManager.SearchPropertyItem(wPropertyIndex);

	//有效效验
	if(pPropertyInfo==NULL)
	{
		//发送消息
		SendPropertyFailure(pISourceUserItem,TEXT("此道具还未启用,购买失败！"), 0L,cbRequestArea);

		return false;
	}

	//自己使用
    if((pPropertyInfo->wIssueArea&PT_SERVICE_AREA_MESELF)==0 && pISourceUserItem==pTargetUserItem) 
	{
		//发送消息
		SendPropertyFailure(pISourceUserItem,TEXT("此道具不可自己使用,购买失败！"), 0L,cbRequestArea);

		return false;
	}

	//玩家使用
	if((pPropertyInfo->wIssueArea&PT_SERVICE_AREA_PLAYER)==0 && pISourceUserItem!=pTargetUserItem) 
	{
		//发送消息
		SendPropertyFailure(pISourceUserItem,TEXT("此道具不可赠送给玩家,只能自己使用,购买失败！"), 0L,cbRequestArea);

		return false;
	}

	//旁观范围
	if((pPropertyInfo->wIssueArea&PT_SERVICE_AREA_LOOKON)==0)  
	{
		//变量定义
		WORD wTableID = pTargetUserItem->GetTableID();
		if(wTableID!=INVALID_TABLE)
		{
			//变量定义
			WORD wEnumIndex=0;
			IServerUserItem * pIServerUserItem=NULL;

			//获取桌子
			CTableFrame * pTableFrame=m_TableFrameArray[wTableID];
	
			//枚举用户
			do
			{
				//获取用户
				pIServerUserItem=pTableFrame->EnumLookonUserItem(wEnumIndex++);
				if( pIServerUserItem==NULL) break;
				if( pIServerUserItem==pTargetUserItem )
				{
					//发送消息
					SendPropertyFailure(pISourceUserItem,TEXT("此道具不可赠送给旁观用户,购买失败！"), 0L,cbRequestArea);

					return false;
				}
			} while (true);
		}
	}

	//道具判断
	switch(wPropertyIndex)
	{
	case PROPERTY_ID_SCORE_CLEAR :			//负分清零
		{
			//变量定义
			SCORE lCurrScore = pTargetUserItem->GetUserScore();
			if( lCurrScore >= 0)
			{
				//变量定义
				TCHAR szMessage[128]=TEXT("");
				if ( pISourceUserItem==pTargetUserItem ) 
					_sntprintf(szMessage,CountArray(szMessage),TEXT("您现在的积分已经是非负数，不需要使用负分清零道具！"));
				else
					_sntprintf(szMessage,CountArray(szMessage),TEXT("[ %s ]现在的积分已经是非负数，不需要使用负分清零道具！"), pTargetUserItem->GetNickName());

				//发送消息
				SendPropertyFailure(pISourceUserItem,szMessage, 0L,cbRequestArea);

				return false;
			}
			break;
		}
	case PROPERTY_ID_ESCAPE_CLEAR :			 //逃跑清零
		{
			//变量定义
			DWORD dwCurrFleeCount = pTargetUserItem->GetUserInfo()->dwFleeCount;
			if ( dwCurrFleeCount==0 )
			{
				//变量定义
				TCHAR szMessage[128]=TEXT("");		
				if ( pISourceUserItem == pTargetUserItem ) 
					_sntprintf(szMessage,CountArray(szMessage),TEXT("您现在的逃跑率已经为0，不需要使用逃跑清零道具！"));
				else
					_sntprintf(szMessage,CountArray(szMessage),TEXT("[ %s ]现在的逃跑率已经为0，不需要使用逃跑清零道具！"), pTargetUserItem->GetNickName());

				//发送消息
				SendPropertyFailure(pISourceUserItem,szMessage,0L,cbRequestArea);

				return false;
			}
			break;
		}
	}

	return true;
}

//绑定用户
IServerUserItem * CAttemperEngineSink::GetBindUserItem(WORD wBindIndex)
{
	//获取参数
	tagBindParameter * pBindParameter=GetBindParameter(wBindIndex);

	//获取用户
	if (pBindParameter!=NULL)
	{
		return pBindParameter->pIServerUserItem;
	}

	//错误断言
	ASSERT(FALSE);

	return NULL;
}

//绑定参数
tagBindParameter * CAttemperEngineSink::GetBindParameter(WORD wBindIndex)
{
	//无效连接
	if (wBindIndex==INVALID_WORD) return NULL;

	//常规连接
	if (wBindIndex<m_pGameServiceOption->wMaxPlayer)
	{
		return m_pNormalParameter+wBindIndex;
	}

	//机器连接
	if ((wBindIndex>=INDEX_ANDROID)&&(wBindIndex<(INDEX_ANDROID+MAX_ANDROID)))
	{
		return m_pAndroidParameter+(wBindIndex-INDEX_ANDROID);
	}

	//错误断言
	ASSERT(FALSE);

	return NULL;
}

//道具类型
WORD CAttemperEngineSink::GetPropertyType(WORD wPropertyIndex)
{
	switch(wPropertyIndex)
	{
	case PROPERTY_ID_CAR:	case PROPERTY_ID_EGG: 	case PROPERTY_ID_CLAP: 	case PROPERTY_ID_KISS: 	case PROPERTY_ID_BEER:
	case PROPERTY_ID_CAKE: 	case PROPERTY_ID_RING:  case PROPERTY_ID_BEAT: 	case PROPERTY_ID_BOMB:  case PROPERTY_ID_SMOKE:
	case PROPERTY_ID_VILLA: case PROPERTY_ID_BRICK: case PROPERTY_ID_FLOWER: 
		{
			return PT_TYPE_PRESENT;
		};
    case PROPERTY_ID_TWO_CARD: 	case PROPERTY_ID_FOUR_CARD:  case PROPERTY_ID_SCORE_CLEAR:     case PROPERTY_ID_ESCAPE_CLEAR:
	case PROPERTY_ID_TRUMPET:	case PROPERTY_ID_TYPHON:     case PROPERTY_ID_GUARDKICK_CARD:  case PROPERTY_ID_POSSESS:
	case PROPERTY_ID_BLUERING_CARD: case PROPERTY_ID_YELLOWRING_CARD: case PROPERTY_ID_WHITERING_CARD: case PROPERTY_ID_REDRING_CARD:
	case PROPERTY_ID_VIPROOM_CARD: 
		{
			return PT_TYPE_PROPERTY;
		};
	}

	ASSERT(false);

	return PT_TYPE_ERROR;
}

//配置机器
bool CAttemperEngineSink::InitAndroidUser()
{
	//机器参数
	tagAndroidUserParameter AndroidUserParameter;
	ZeroMemory(&AndroidUserParameter,sizeof(AndroidUserParameter));

	//配置参数
	AndroidUserParameter.pGameParameter=m_pGameParameter;
	AndroidUserParameter.pGameServiceAttrib=m_pGameServiceAttrib;
	AndroidUserParameter.pGameServiceOption=m_pGameServiceOption;
	AndroidUserParameter.pGameServiceTableOption = m_pGameServiceTableOption;

	//服务组件
	AndroidUserParameter.pITimerEngine=m_pITimerEngine;
	AndroidUserParameter.pIServerUserManager=&m_ServerUserManager;
	AndroidUserParameter.pIGameServiceManager=m_pIGameServiceManager;
	AndroidUserParameter.pITCPNetworkEngineEvent=QUERY_OBJECT_PTR_INTERFACE(m_pIAttemperEngine,ITCPNetworkEngineEvent);

	//设置对象
	if (m_AndroidUserManager.InitAndroidUser(AndroidUserParameter)==false)
	{
		return false;
	}

	return true;
}

//配置桌子
bool CAttemperEngineSink::InitTableFrameArray()
{
	//桌子参数
	tagTableFrameParameter TableFrameParameter;
	ZeroMemory(&TableFrameParameter,sizeof(TableFrameParameter));

	//内核组件
	TableFrameParameter.pITimerEngine=m_pITimerEngine;
	TableFrameParameter.pIKernelDataBaseEngine=m_pIKernelDataBaseEngine;
	TableFrameParameter.pIRecordDataBaseEngine=m_pIRecordDataBaseEngine;

	//服务组件
	TableFrameParameter.pIMainServiceFrame=this;
	TableFrameParameter.pIAndroidUserManager=&m_AndroidUserManager;
	TableFrameParameter.pIGameServiceManager=m_pIGameServiceManager;

	//配置参数
	TableFrameParameter.pGameParameter=m_pGameParameter;
	TableFrameParameter.pGameServiceAttrib=m_pGameServiceAttrib;
	TableFrameParameter.pGameServiceOption=m_pGameServiceOption;

	if(m_pIGameMatchServiceManager!=NULL)
		TableFrameParameter.pIGameMatchServiceManager=QUERY_OBJECT_PTR_INTERFACE(m_pIGameMatchServiceManager,IUnknownEx);

	//桌子容器
	m_TableFrameArray.resize(m_pGameServiceOption->wTableCount);

	//创建桌子
	for (WORD i=0;i<m_pGameServiceOption->wTableCount;i++)
	{
		//创建对象
		m_TableFrameArray[i]=new CTableFrame;

		//配置桌子
		TableFrameParameter.pTableOption = &(m_pGameServiceTableOption->TableOption[i]);
		if (m_TableFrameArray[i]->InitializationFrame(i,TableFrameParameter)==false)
		{
			return false;
		}

		if(m_pIGameMatchServiceManager!=NULL)
			m_pIGameMatchServiceManager->InitTableFrame(QUERY_OBJECT_PTR_INTERFACE((m_TableFrameArray[i]),ITableFrame),i);
	}

	if(m_pIGameMatchServiceManager!=NULL)
	{
		if (m_pIGameMatchServiceManager->InitMatchInterface(QUERY_OBJECT_PTR_INTERFACE(m_pIAttemperEngine,ITCPNetworkEngineEvent),m_pIKernelDataBaseEngine,
				(IServerUserManager*)QUERY_OBJECT_INTERFACE(m_ServerUserManager,IServerUserManager),this,this,m_pITimerEngine,&m_AndroidUserManager)==false)
		{
			ASSERT(FALSE);
			return false;
		}
	}

	return true;
}

//配置广场赛
bool CAttemperEngineSink::InitSquareMatch()
{
	LOGI("CAttemperEngineSink::InitSquareMatch 初始化广场赛");
	//桌子参数
	tagSquareMatchParameter SquareMatchParameter;
	ZeroMemory(&SquareMatchParameter,sizeof(SquareMatchParameter));

	//内核组件
	SquareMatchParameter.pITimerEngine=m_pITimerEngine;
	SquareMatchParameter.pIKernelDataBaseEngine=m_pIKernelDataBaseEngine;
	SquareMatchParameter.pIRecordDataBaseEngine=m_pIRecordDataBaseEngine;

	//服务组件
	SquareMatchParameter.pIMainServiceFrame=this;
	SquareMatchParameter.pIServerUserManager=&m_ServerUserManager;

	//配置参数
	SquareMatchParameter.pGameParameter=m_pGameParameter;
	SquareMatchParameter.pGameServiceAttrib=m_pGameServiceAttrib;
	SquareMatchParameter.pGameServiceOption=m_pGameServiceOption;
	//for(BYTE i=0; i<m_cbGameSquareOptionArrayCount; i++)
	//{
	//	SquareMatchParameter.pGameSquareOption=m_pGameSquareOptionArray[i];

	//	CSquare Square;
	//	Square.InitSquareMatch(SquareMatchParameter);
	//	m_SquareManager[m_pGameSquareOptionArray[i]->wMatchID]=Square;
	//}
	for(BYTE i=0; i<m_cbGameSquareOptionArrayCount; i++)
	{
		SquareMatchParameter.pGameSquareOption=m_pGameSquareOptionArray[i];

		CSquare Square;
		Square.InitSquareMatch(SquareMatchParameter);
		m_SquareManager[m_pGameSquareOptionArray[i]->wMatchID]=Square;
	}

	for(map<WORD,CSquare>::iterator iter=m_SquareManager.begin(); iter!=m_SquareManager.end(); iter++)
	{
		CSquare Square=iter->second;

		TCHAR szStartToEndTime[1024]=TEXT("");
		_sntprintf(szStartToEndTime,CountArray(szStartToEndTime),\
			TEXT("%d-%02d-%02d %02d:%02d:%02d - %d-%02d-%02d %02d:%02d:%02d"),\
			iter->second.m_pGameSquareOption->StartTime.wYear,iter->second.m_pGameSquareOption->StartTime.wMonth,iter->second.m_pGameSquareOption->StartTime.wDay,\
			iter->second.m_pGameSquareOption->StartTime.wHour,iter->second.m_pGameSquareOption->StartTime.wMinute,iter->second.m_pGameSquareOption->StartTime.wSecond,\
			iter->second.m_pGameSquareOption->EndTime.wYear,iter->second.m_pGameSquareOption->EndTime.wMonth,iter->second.m_pGameSquareOption->EndTime.wDay,\
			iter->second.m_pGameSquareOption->EndTime.wHour,iter->second.m_pGameSquareOption->EndTime.wMinute,iter->second.m_pGameSquareOption->EndTime.wSecond);
		LOGI("CAttemperEngineSink::InitSquareMatch 广场赛配置: MatchID= " << iter->second.m_pGameSquareOption->wMatchID\
			<< " SquareName= " << iter->second.m_pGameSquareOption->szSquareName << " ServerID= " << iter->second.m_pGameSquareOption->wServerID\
			<< " GameID= " << iter->second.m_pGameSquareOption->wGameID << " StartToEndTime= " << szStartToEndTime << " Interval= " << iter->second.m_pGameSquareOption->wInterval\
			<< " RankNumber= " << iter->second.m_pGameSquareOption->wRankNumber << " SquareDesc= " << iter->second.m_pGameSquareOption->szSquareDesc\
			<< " SquareBroadcast= " << iter->second.m_pGameSquareOption->szSquareBroadcast << " RankBonusCount= " << iter->second.m_pGameSquareOption->cbRankBonus\
			<< " RankBonusByResultCount= " << iter->second.m_pGameSquareOption->cbRankBonusByResult);

		LOGI("CAttemperEngineSink::InitSquareMatch 广场赛奖励配置 RankBonusCount= " << iter->second.m_pGameSquareOption->cbRankBonus);

		for(int i=0; i<iter->second.m_pGameSquareOption->cbRankBonus; i++)
		{
			LOGI("CAttemperEngineSink::InitSquareMatch i= " << i << " First= " << iter->second.m_pGameSquareOption->RankBonus[i].wFirst\
				<< " Last= " << iter->second.m_pGameSquareOption->RankBonus[i].wLast\
				<< " PropertyCount= " << iter->second.m_pGameSquareOption->RankBonus[i].cbPropertyCount);

			for(int j=0; j<iter->second.m_pGameSquareOption->RankBonus[i].cbPropertyCount; j++)
			{
				LOGI("CAttemperEngineSink::InitSquareMatch j= " << j << " PropertyID= " << iter->second.m_pGameSquareOption->RankBonus[i].wPropertyID[j]);
			}
		}
	}

	return true;
}

//更新广场赛
bool CAttemperEngineSink::UpdateSquareMatch(tagGameSquareOption* pGameSquareOption)
{
	if(pGameSquareOption->wMatchID<=0)return false;

 	map<WORD,CSquare>::iterator iter=m_SquareManager.find(pGameSquareOption->wMatchID);
 	if(iter!=m_SquareManager.end())
 	{
 		if(iter->second.m_bStartSquare)
 		{
 			LOGE("CAttemperEngineSink::UpdateSquareMatch 更新广场赛失败, 广场赛已开始 wMatch= " << pGameSquareOption->wMatchID << " SquareName= " << iter->second.m_pGameSquareOption->szSquareName);
 			return false;
 		}
 		memcpy(iter->second.m_pGameSquareOption, pGameSquareOption, sizeof(tagGameSquareOption));

		//构造提示
		TCHAR szString[256]=TEXT("");
		LPCTSTR pszServerName=iter->second.m_pGameSquareOption->szSquareName;
		_sntprintf(szString,CountArray(szString),TEXT("[ %s ] 广场赛参数覆盖加载成功"),pszServerName);

		//输出信息
		CTraceService::TraceString(szString,TraceLevel_Normal);
 	}
 	else
 	{
		//桌子参数
		tagSquareMatchParameter SquareMatchParameter;
		ZeroMemory(&SquareMatchParameter,sizeof(SquareMatchParameter));

		//内核组件
		SquareMatchParameter.pITimerEngine=m_pITimerEngine;
		SquareMatchParameter.pIKernelDataBaseEngine=m_pIKernelDataBaseEngine;
		SquareMatchParameter.pIRecordDataBaseEngine=m_pIRecordDataBaseEngine;

		//服务组件
		SquareMatchParameter.pIMainServiceFrame=this;
		SquareMatchParameter.pIServerUserManager=&m_ServerUserManager;

		//配置参数
		SquareMatchParameter.pGameParameter=m_pGameParameter;
		SquareMatchParameter.pGameServiceAttrib=m_pGameServiceAttrib;
		SquareMatchParameter.pGameServiceOption=m_pGameServiceOption;
		SquareMatchParameter.pGameSquareOption=pGameSquareOption;

		CSquare Square;
		Square.InitSquareMatch(SquareMatchParameter);
		m_SquareManager[pGameSquareOption->wMatchID]=Square;

		//构造提示
		TCHAR szString[256]=TEXT("");
		LPCTSTR pszServerName=pGameSquareOption->szSquareName;
		_sntprintf(szString,CountArray(szString),TEXT("[ %s ] 广场赛参数添加加载成功"),pszServerName);

		//输出信息
		CTraceService::TraceString(szString,TraceLevel_Normal);
	}

// 	for(map<WORD,CSquare>::iterator iter=m_SquareManager.begin(); iter!=m_SquareManager.end(); iter++)
// 	{
// 		CSquare Square=iter->second;
// 
// 		LOGI("CAttemperEngineSink::UpdateSquareMatch 更新广场赛 wMatch= "\
// 			<< iter->second.m_pGameSquareOption->wMatchID << " SquareName= "\
// 			<< iter->second.m_pGameSquareOption->szSquareName);
// 	}

}

//发送请求
bool CAttemperEngineSink::SendUIControlPacket(WORD wRequestID, VOID * pData, WORD wDataSize)
{
	//发送数据
	CServiceUnits * pServiceUnits=CServiceUnits::g_pServiceUnits;
	pServiceUnits->PostControlRequest(wRequestID,pData,wDataSize);

	return true;
}

//插入分配
bool CAttemperEngineSink::InsertDistribute(IServerUserItem * pIServerUserItem)
{
	//效验参数
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//状态判断
	ASSERT(pIServerUserItem->GetUserStatus()<US_PLAYING);
	if (pIServerUserItem->GetUserStatus()>=US_PLAYING) return false;

	bool bReturn=false;
	if(m_pIGameMatchServiceManager!=NULL) bReturn=m_pIGameMatchServiceManager->OnUserJoinGame(pIServerUserItem,0);
	return bReturn;
}

//敏感词过滤
void CAttemperEngineSink::SensitiveWordFilter(LPCTSTR pMsg, LPTSTR pszFiltered, int nMaxLen)
{
	m_WordsFilter.Filtrate(pMsg,pszFiltered,nMaxLen);
}

// 计算平衡分调整(added by anjay )
LONG CAttemperEngineSink::CalculateBalanceScore(SCORE lOldScore, SCORE lNewScore)
{
	// 判断当前房间是否适用运气值，不适用的话，不进行计算
	if (CServerRule::IsForfendCalculateBalanceScore(m_pGameServiceOption->dwServerRule))
	{
		return 0;
	}
	else
	{
		return m_BalanceScoreCurve.CalculateBalanceScore(lOldScore, lNewScore);
	}
}

//计算广场赛积分
BYTE CAttemperEngineSink::ResultSquareMatchScore(ITableFrame* pITableFrame, tagScoreInfo ScoreInfoArray[], WORD wScoreCount)
{
	//广场赛计分
	//for (size_t stSquareIndex=0; stSquareIndex<m_SquareManager.size(); stSquareIndex++)
	//{
	//	if (m_SquareManager[stSquareIndex].m_bStartSquare)
	//		m_SquareManager[stSquareIndex].ResultToRank(pITableFrame, ScoreInfoArray, wScoreCount);
	//}
	for (map<WORD,CSquare>::iterator iter=m_SquareManager.begin(); iter!=m_SquareManager.end(); iter++)
	{
		if(iter->second.m_bStartSquare)iter->second.ResultToRank(pITableFrame, ScoreInfoArray, wScoreCount);
	}
	return 0;
}

//计算广场赛积分
BYTE CAttemperEngineSink::ResultSquareMatchScore(ITableFrame* pITableFrame, WORD wChairID, tagScoreInfo ScoreInfo)
{
	//广场赛计分
	//for (size_t stSquareIndex=0; stSquareIndex<m_SquareManager.size(); stSquareIndex++)
	//{
	//	if (m_SquareManager[stSquareIndex].m_bStartSquare)
	//		m_SquareManager[stSquareIndex].ResultToRank(pITableFrame, ScoreInfoArray, wScoreCount);
	//}
	for (map<WORD,CSquare>::iterator iter=m_SquareManager.begin(); iter!=m_SquareManager.end(); iter++)
	{
		if(iter->second.m_bStartSquare)iter->second.ResultToRank(pITableFrame, wChairID, ScoreInfo);
	}
	return 0;
}

//插入用户
bool CAttemperEngineSink::InsertWaitDistribute(IServerUserItem * pIServerUserItem)
{
	//查找
	bool bAdd=true;
	POSITION pos=m_WaitDistributeList.GetHeadPosition();
	while(pos != NULL)
	{
		IServerUserItem *pUserItem = m_WaitDistributeList.GetNext(pos);
		if(pUserItem->GetUserID()==pIServerUserItem->GetUserID())
		{
			bAdd=false;
			break;
		}
	}

	//加入
	if(bAdd) m_WaitDistributeList.AddTail(pIServerUserItem);

	//通知
	SendData(pIServerUserItem,MDM_GR_USER,SUB_GR_USER_WAIT_DISTRIBUTE,NULL,0);

	return bAdd;
}

//删除用户
bool CAttemperEngineSink::DeleteWaitDistribute(IServerUserItem * pIServerUserItem)
{
	//查找
	POSITION pos=m_WaitDistributeList.GetHeadPosition();
	while(pos != NULL)
	{
		POSITION tempPos = pos;
		IServerUserItem *pUserItem = m_WaitDistributeList.GetNext(pos);
		if(pUserItem->GetUserID()==pIServerUserItem->GetUserID())
		{
			m_WaitDistributeList.RemoveAt(tempPos);
			break;
		}
	}

	return true;
}

//分配用户
bool CAttemperEngineSink::DistributeUserGame()
{
	//用户数判断
	WORD wNeedUserCount=__max(m_pGameServiceAttrib->wChairCount, m_pGameServiceOption->wMinDistributeUser);
	if(m_pGameServiceAttrib->cbDynamicJoin==TRUE)wNeedUserCount=2;
	if(m_WaitDistributeList.GetCount()<wNeedUserCount)return true;

	//随机分配
	for (INT_PTR i=0;i<m_pGameServiceOption->wTableCount;i++)
	{
		//获取桌子
		CTableFrame * pTableFrame=m_TableFrameArray[i];
		if ((pTableFrame->IsGameStarted()==true)) continue;

		//桌子状况
		tagTableUserInfo TableUserInfo;
		WORD wUserSitCount=pTableFrame->GetTableUserInfo(TableUserInfo);

		//分配判断
		if (wUserSitCount!=0) continue;

		//坐下判断
		if (m_WaitDistributeList.GetCount()>=TableUserInfo.wMinUserCount)
		{
			//变量定义
			WORD wHandleCount=0;
			WORD wWantUserCount=TableUserInfo.wMinUserCount;
			WORD wFreeUserCount=(WORD)m_WaitDistributeList.GetCount();

			//数据调整
			if (m_pGameServiceAttrib->wChairCount>TableUserInfo.wMinUserCount)
			{
				WORD wChairCount=m_pGameServiceAttrib->wChairCount;
				WORD wOffUserCount=__min(wChairCount,wFreeUserCount)-TableUserInfo.wMinUserCount;
				wWantUserCount+=(wOffUserCount > 0) ? (rand()%wOffUserCount) : 0;
			}

			//坐下处理
			POSITION pos=m_WaitDistributeList.GetHeadPosition();
			while(pos!=NULL)
			{
				//变量定义
				WORD wChairID=pTableFrame->GetRandNullChairID();

				//无效过滤
				if (wChairID==INVALID_CHAIR) continue;

				//坐下处理
				POSITION tempPos=pos;
				IServerUserItem *pUserItem = m_WaitDistributeList.GetNext(pos);
				if(pUserItem && pUserItem->GetUserStatus()==US_FREE && (pTableFrame->PerformSitDownAction(wChairID,pUserItem)==true))
				{
					//删除排队
					m_WaitDistributeList.RemoveAt(tempPos);

					//设置变量
					wHandleCount++;

					//完成判断
					if (wHandleCount>=wWantUserCount) 
					{
						return true;
					}
				}
			}

			if(wHandleCount > 0) return true;
		}
	}

	return true;
}

//设置参数
void CAttemperEngineSink::SetMobileUserParameter(IServerUserItem * pIServerUserItem,BYTE cbDeviceType,WORD wBehaviorFlags,WORD wPageTableCount)
{
	if(wPageTableCount > m_pGameServiceOption->wTableCount)wPageTableCount=m_pGameServiceOption->wTableCount;
	pIServerUserItem->SetMobileUserDeskCount(wPageTableCount);
	pIServerUserItem->SetMobileUserRule(wBehaviorFlags);
}

//群发数据
bool CAttemperEngineSink::SendDataBatchToMobileUser(WORD wCmdTable, WORD wMainCmdID, WORD wSubCmdID, VOID * pData, WORD wDataSize)
{
	//枚举用户
	WORD wEnumIndex=0;
	while(wEnumIndex<m_ServerUserManager.GetUserItemCount())
	{
		//过滤用户
		IServerUserItem *pIServerUserItem=m_ServerUserManager.EnumUserItem(wEnumIndex++);
		if(pIServerUserItem==NULL) continue;
		if(!pIServerUserItem->IsMobileUser()) continue;

		//定义变量
		WORD wMobileUserRule = pIServerUserItem->GetMobileUserRule();
		WORD wTagerTableID = pIServerUserItem->GetTableID();
		bool bViewModeAll = ((wMobileUserRule&VIEW_MODE_ALL)!=0);
		bool bRecviceGameChat = ((wMobileUserRule&RECVICE_GAME_CHAT)!=0);
		bool bRecviceRoomChat = ((wMobileUserRule&RECVICE_ROOM_CHAT)!=0);
		bool bRecviceRoomWhisper = ((wMobileUserRule&RECVICE_ROOM_WHISPER)!=0);

		//状态过滤
		if(pIServerUserItem->GetUserStatus() >= US_SIT)
		{
			if(wTagerTableID != wCmdTable)continue;
		}

		//聊天过滤
		if(wSubCmdID==SUB_GR_USER_CHAT || wSubCmdID==SUB_GR_USER_EXPRESSION)
		{
			if(!bRecviceGameChat || !bRecviceRoomChat) continue;
		}
		if(wSubCmdID==SUB_GR_WISPER_CHAT || wSubCmdID==SUB_GR_WISPER_EXPRESSION)
		{
			if(!bRecviceRoomWhisper) continue;
		}

		//部分可视
		if(!bViewModeAll)
		{
			//消息过滤
			if(wSubCmdID==SUB_GR_USER_ENTER && wCmdTable==INVALID_TABLE) continue;
			if(wSubCmdID==SUB_GR_USER_SCORE && pIServerUserItem->GetUserStatus() < US_SIT) continue;
			
			//定义变量
			WORD wTagerDeskPos = pIServerUserItem->GetMobileUserDeskPos();
			WORD wTagerDeskCount = pIServerUserItem->GetMobileUserDeskCount();

			//状态消息过滤
			if(wCmdTable < wTagerDeskPos) continue;
			if(wCmdTable > (wTagerDeskPos+wTagerDeskCount-1)) continue;
		}

		//发送消息
		SendData(pIServerUserItem,wMainCmdID,wSubCmdID,pData,wDataSize);
	}

	return true;
}

//群发用户信息
bool CAttemperEngineSink::SendUserInfoPacketBatchToMobileUser(IServerUserItem * pIServerUserItem)
{
	//效验参数
	ASSERT(pIServerUserItem!=NULL);
	if (pIServerUserItem==NULL) return false;

	//变量定义
	BYTE cbBuffer[SOCKET_TCP_PACKET];
	tagUserInfo * pUserInfo=pIServerUserItem->GetUserInfo();
	tagMobileUserInfoHead * pUserInfoHead=(tagMobileUserInfoHead *)cbBuffer;
	CSendPacketHelper SendPacket(cbBuffer+sizeof(tagMobileUserInfoHead),sizeof(cbBuffer)-sizeof(tagMobileUserInfoHead));

	//用户属性
	pUserInfoHead->wFaceID=pUserInfo->wFaceID;
	pUserInfoHead->dwGameID=pUserInfo->dwGameID;
	pUserInfoHead->dwUserID=pUserInfo->dwUserID;
	pUserInfoHead->dwCustomID=pUserInfo->dwCustomID;

	//用户属性
	pUserInfoHead->cbGender=pUserInfo->cbGender;
	pUserInfoHead->cbMemberOrder=pUserInfo->cbMemberOrder;

	//用户状态
	pUserInfoHead->wTableID=pUserInfo->wTableID;
	pUserInfoHead->wChairID=pUserInfo->wChairID;
	pUserInfoHead->cbUserStatus=pUserInfo->cbUserStatus;

	//用户局数
	pUserInfoHead->dwWinCount=pUserInfo->dwWinCount;
	pUserInfoHead->dwLostCount=pUserInfo->dwLostCount;
	pUserInfoHead->dwDrawCount=pUserInfo->dwDrawCount;
	pUserInfoHead->dwFleeCount=pUserInfo->dwFleeCount;
	pUserInfoHead->dwExperience=pUserInfo->dwExperience;

	//用户游戏币
	pUserInfoHead->lScore=pUserInfo->lScore;
	pUserInfoHead->lScore+=pIServerUserItem->GetTrusteeScore();
	pUserInfoHead->lScore+=pIServerUserItem->GetFrozenedScore();

	//叠加信息
	SendPacket.AddPacket(pUserInfo->szNickName,DTP_GR_NICK_NAME);

	//发送数据
	WORD wHeadSize=sizeof(tagMobileUserInfoHead);
	SendDataBatchToMobileUser(pUserInfoHead->wTableID,MDM_GR_USER,SUB_GR_USER_ENTER,cbBuffer,wHeadSize+SendPacket.GetDataSize());

	return true;
}

//发可视用户信息(发送某个玩家的信息，给场上所有的玩家)
bool CAttemperEngineSink::SendViewTableUserInfoPacketToMobileUser(IServerUserItem * pIServerUserItem,DWORD dwUserIDReq)
{
	//变量定义
	BYTE cbBuffer[SOCKET_TCP_PACKET];
	tagMobileUserInfoHead * pUserInfoHead=(tagMobileUserInfoHead *)cbBuffer;
	WORD wMobileUserRule = pIServerUserItem->GetMobileUserRule();
	WORD wTagerTableID = pIServerUserItem->GetTableID();
	WORD wTagerDeskPos = pIServerUserItem->GetMobileUserDeskPos();
	WORD wTagerDeskCount = pIServerUserItem->GetMobileUserDeskCount();
	bool bViewModeAll = ((wMobileUserRule&VIEW_MODE_ALL)!=0);
	if(wTagerDeskCount==0) wTagerDeskCount=1;

	//枚举用户
	WORD wEnumIndex=0;
	while(wEnumIndex<m_ServerUserManager.GetUserItemCount())
	{
		//过滤用户
		IServerUserItem *pIUserItem=m_ServerUserManager.EnumUserItem(wEnumIndex++);
		if(pIUserItem==NULL || (dwUserIDReq==INVALID_CHAIR && pIUserItem==pIServerUserItem)) continue;
		if(dwUserIDReq != INVALID_CHAIR && pIUserItem->GetUserID() != dwUserIDReq) continue;

		//部分可视
		if(dwUserIDReq==INVALID_CHAIR && !bViewModeAll)
		{
			if(pIUserItem->GetTableID() < wTagerDeskPos) continue;
			if(pIUserItem->GetTableID() > (wTagerDeskPos+wTagerDeskCount-1)) continue;
		}

		//定义变量
		tagUserInfo * pUserInfo=pIUserItem->GetUserInfo();
		ZeroMemory(cbBuffer,sizeof(cbBuffer));
		CSendPacketHelper SendPacket(cbBuffer+sizeof(tagMobileUserInfoHead),sizeof(cbBuffer)-sizeof(tagMobileUserInfoHead));

		//用户属性
		pUserInfoHead->wFaceID=pUserInfo->wFaceID;
		pUserInfoHead->dwGameID=pUserInfo->dwGameID;
		pUserInfoHead->dwUserID=pUserInfo->dwUserID;
		pUserInfoHead->dwCustomID=pUserInfo->dwCustomID;

		//用户属性
		pUserInfoHead->cbGender=pUserInfo->cbGender;
		pUserInfoHead->cbMemberOrder=pUserInfo->cbMemberOrder;

		//用户状态
		pUserInfoHead->wTableID=pUserInfo->wTableID;
		pUserInfoHead->wChairID=pUserInfo->wChairID;
		pUserInfoHead->cbUserStatus=pUserInfo->cbUserStatus;

		//用户局数
		pUserInfoHead->dwWinCount=pUserInfo->dwWinCount;
		pUserInfoHead->dwLostCount=pUserInfo->dwLostCount;
		pUserInfoHead->dwDrawCount=pUserInfo->dwDrawCount;
		pUserInfoHead->dwFleeCount=pUserInfo->dwFleeCount;
		pUserInfoHead->dwExperience=pUserInfo->dwExperience;

		//用户游戏币
		pUserInfoHead->lScore=pUserInfo->lScore;
		pUserInfoHead->lScore+=pIUserItem->GetTrusteeScore();
		pUserInfoHead->lScore+=pIUserItem->GetFrozenedScore();

		//叠加信息
		SendPacket.AddPacket(pUserInfo->szNickName,DTP_GR_NICK_NAME);

		//发送数据
		WORD wHeadSize=sizeof(tagMobileUserInfoHead);
		SendData(pIServerUserItem,MDM_GR_USER,SUB_GR_USER_ENTER,cbBuffer,wHeadSize+SendPacket.GetDataSize());
	}

	return true;
}

//手机立即登录
bool CAttemperEngineSink::MobileUserImmediately(IServerUserItem * pIServerUserItem)
{
	//查找桌子
	for (INT_PTR i=0;i<(m_pGameServiceOption->wTableCount);i++)
	{
		//获取桌子
		CTableFrame * pTableFrame=m_TableFrameArray[i];
		if (pTableFrame->IsGameStarted()==true) continue;
		if(pTableFrame->IsTableLocked()) continue;

		//无效过滤
		WORD wChairID=pTableFrame->GetRandNullChairID();
		if (wChairID==INVALID_CHAIR) continue;

		//用户坐下
		pTableFrame->PerformSitDownAction(wChairID,pIServerUserItem);
		return true;
	}

	//失败
	m_TableFrameArray[0]->SendRequestFailure(pIServerUserItem,TEXT("没找到可进入的游戏桌！"),REQUEST_FAILURE_NORMAL);
	return true;
}

//发送系统消息
bool CAttemperEngineSink::SendSystemMessage(CMD_GR_SendMessage * pSendMessage, WORD wDataSize)
{
	//消息处理
	ASSERT(pSendMessage!=NULL);

	//效验数据
	ASSERT(wDataSize==sizeof(CMD_GR_SendMessage)-sizeof(pSendMessage->szSystemMessage)+sizeof(TCHAR)*pSendMessage->wChatLength);
	if (wDataSize!=sizeof(CMD_GR_SendMessage)-sizeof(pSendMessage->szSystemMessage)+sizeof(TCHAR)*pSendMessage->wChatLength) 
		return false;

	//所有房间
	if(pSendMessage->cbAllRoom == TRUE)
	{
		pSendMessage->cbAllRoom=FALSE;
		m_pITCPSocketService->SendData(MDM_CS_MANAGER_SERVICE,SUB_CS_C_SYSTEM_MESSAGE,pSendMessage,wDataSize);

//		CTraceService::TraceString(TEXT("run sendsystemmessage！"),TraceLevel_Exception);
	}
	else
	{
		//发送系统消息
		if(pSendMessage->cbGame == TRUE)
			SendGameMessage(pSendMessage->szSystemMessage, SMT_CHAT | SMT_TABLE_ROLL);
		if(pSendMessage->cbRoom == TRUE)
			SendRoomMessage(pSendMessage->szSystemMessage, SMT_CHAT | SMT_TABLE_ROLL);
	}

	return true;
}

//清除消息数据
void CAttemperEngineSink::ClearSystemMessageData()
{
	while(m_SystemMessageList.GetCount() > 0)
	{
		tagSystemMessage * pRqHead = m_SystemMessageList.RemoveHead();
		if(pRqHead)
		{
			delete [] ((BYTE*)pRqHead);
		}
	}
}
//////////////////////////////////////////////////////////////////////////////////
