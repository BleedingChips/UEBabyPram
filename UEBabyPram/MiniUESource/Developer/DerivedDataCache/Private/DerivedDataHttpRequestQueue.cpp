// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataHttpRequestQueue.h"

#include "Async/ManualResetEvent.h"
#include "DerivedDataRequest.h"
#include "DerivedDataRequestOwner.h"
#include "Misc/ScopeExit.h"

namespace UE::DerivedData
{

class FHttpRequestQueue::FQueueRequest : FRequestBase
{
public:
	FQueueRequest(IRequestOwner& InOwner, FOnRequest&& InOnRequest)
		: Owner(InOwner)
		, OnRequest(MoveTemp(InOnRequest))
	{
		AddRef(); // Release() is called by ClaimRequest()
		Owner.Begin(this);
	}

	bool TryClaimRequest(THttpUniquePtr<IHttpRequest>&& Request)
	{
		ON_SCOPE_EXIT{ Release(); };
		return TryComplete(MoveTemp(Request));
	}

private:
	bool TryComplete(THttpUniquePtr<IHttpRequest>&& Request)
	{
		if (bComplete.exchange(true))
		{
			return false;
		}
		Owner.End(this, [this](THttpUniquePtr<IHttpRequest>&& Request)
		{
			OnRequest(MoveTemp(Request));
			OnComplete.Notify();
		}, MoveTemp(Request));
		return true;
	}

	void SetPriority(EPriority Priority) final
	{
	}

	void Cancel() final
	{
		if (!TryComplete({}))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_CancelOperation);
			OnComplete.Wait();
		}
	}

	void Wait() final
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_WaitOperation);
		OnComplete.Wait();
	}

	IRequestOwner& Owner;
	FOnRequest OnRequest;
	FManualResetEvent OnComplete;
	std::atomic<bool> bComplete = false;
};

void FHttpRequestQueue::Initialize(IHttpConnectionPool& ConnectionPool, const FHttpClientParams& ClientParams)
{
	FHttpClientParams QueueParams = ClientParams;
	QueueParams.OnDestroyRequest = [this, OnDestroyRequest = MoveTemp(QueueParams.OnDestroyRequest)]
		{
			if (OnDestroyRequest)
			{
				OnDestroyRequest();
			}
			if (!Queue.IsEmpty())
			{
				if (THttpUniquePtr<IHttpRequest> Request = Client->TryCreateRequest({}))
				{
					TryGiveRequestToQueue(MoveTemp(Request));
				}
			}
		};
	Client = ConnectionPool.CreateClient(QueueParams);
}

void FHttpRequestQueue::CreateRequestAsync(IRequestOwner& Owner, const FHttpRequestParams& Params, FOnRequest&& OnRequest)
{
	if (Params.bIgnoreMaxRequests)
	{
		THttpUniquePtr<IHttpRequest> Request = Client->TryCreateRequest(Params);
		checkf(Request, TEXT("IHttpClient::TryCreateRequest returned null in spite of bIgnoreMaxRequests."));
		OnRequest(MoveTemp(Request));
		return;
	}

	while (THttpUniquePtr<IHttpRequest> Request = Client->TryCreateRequest(Params))
	{
		if (!TryGiveRequestToQueue(MoveTemp(Request)))
		{
			OnRequest(MoveTemp(Request));
			return;
		}
	}

	Queue.Push(new FQueueRequest(Owner, MoveTemp(OnRequest)));

	while (THttpUniquePtr<IHttpRequest> Request = Client->TryCreateRequest(Params))
	{
		if (!TryGiveRequestToQueue(MoveTemp(Request)))
		{
			return;
		}
	}
}

bool FHttpRequestQueue::TryGiveRequestToQueue(THttpUniquePtr<IHttpRequest>&& Request)
{
	while (FQueueRequest* Waiter = Queue.Pop())
	{
		if (Waiter->TryClaimRequest(MoveTemp(Request)))
		{
			return true;
		}
	}
	return false;
}

} // UE::DerivedData
