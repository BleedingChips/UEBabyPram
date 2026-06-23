// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/LockFreeList.h"
#include "Http/HttpClient.h"

template <typename FuncType> class TUniqueFunction;

namespace UE::DerivedData { class IRequestOwner; }

namespace UE::DerivedData
{

class FHttpRequestQueue
{
public:
	using FOnRequest = TUniqueFunction<void(THttpUniquePtr<IHttpRequest>&& Request)>;

	void Initialize(IHttpConnectionPool& ConnectionPool, const FHttpClientParams& ClientParams);

	void CreateRequestAsync(IRequestOwner& Owner, const FHttpRequestParams& Params, FOnRequest&& OnRequest);

private:
	bool TryGiveRequestToQueue(THttpUniquePtr<IHttpRequest>&& Request);

	class FQueueRequest;

	THttpUniquePtr<IHttpClient> Client;
	TLockFreePointerListFIFO<FQueueRequest, 0> Queue;
};

} // UE::DerivedData
