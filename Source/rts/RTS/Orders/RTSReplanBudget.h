#pragma once
#include "CoreMinimal.h"

/** Game-thread FIFO admission for local planning jobs, not a path failure result. */
struct FRTSReplanBudget
{
	static constexpr int32 JobsPerFrame = 2;

	bool TryBegin(uint32 Requester, uint64 Frame)
	{
		if (Frame != CurrentFrame)
		{
			CurrentFrame = Frame;
			Granted.Reset();
		}
		// Callers retry every frame while waiting. Expire cancelled/dead requests
		// without letting a missing head indefinitely block the remaining queue.
		Pending.RemoveAll([Frame](const FRequest& Request) { return Frame > Request.LastSeen + 2; });
		if (Granted.Contains(Requester)) { return true; }
		FRequest* Request = Pending.FindByPredicate([Requester](const FRequest& Item) { return Item.Id == Requester; });
		if (Request) { Request->LastSeen = Frame; }
		else { Pending.Add({Requester, Frame}); }
		if (Granted.Num() >= JobsPerFrame || Pending[0].Id != Requester) { return false; }
		Pending.RemoveAt(0);
		Granted.Add(Requester);
		return true;
	}

private:
	struct FRequest { uint32 Id; uint64 LastSeen; };
	uint64 CurrentFrame = MAX_uint64;
	TArray<FRequest> Pending;
	TArray<uint32> Granted;
};
