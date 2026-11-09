"""
Task 5: Python simulation of contracts/IPLicense.sol.
Same rules as the Solidity contract; every call is mined into our local chain
as an event, so the access log is tamper-evident too.
"""
import time
from blockchain import Blockchain
from hashing import sha256_file


class IPLicenseContract:
    def __init__(self, chain):
        self.chain = chain
        self.works = {}         # file_hash -> {"owner", "registered_at"}
        self.access_until = {}  # (file_hash, user) -> expiry
        self.now = time.time    # swappable clock so we can simulate expiry

    def _emit(self, event, **kw):
        b = self.chain.add({"type": "contract_event", "event": event, **kw})
        print(f"  [block {b.index}] {event}: {kw}")

    def register_work(self, sender, file_hash):
        if file_hash in self.works:
            raise ValueError("Already registered")
        self.works[file_hash] = {"owner": sender, "registered_at": self.now()}
        self._emit("WorkRegistered", file_hash=file_hash[:12] + "...", owner=sender)

    def grant_access(self, sender, file_hash, user, duration):
        if self.works[file_hash]["owner"] != sender:
            raise PermissionError("Not the owner")
        self.access_until[(file_hash, user)] = self.now() + duration
        self._emit("AccessGranted", user=user, seconds=duration)

    def revoke_access(self, sender, file_hash, user):
        if self.works[file_hash]["owner"] != sender:
            raise PermissionError("Not the owner")
        self.access_until[(file_hash, user)] = 0
        self._emit("AccessRevoked", user=user)

    def request_access(self, sender, file_hash):
        granted = (sender == self.works[file_hash]["owner"]
                   or self.now() <= self.access_until.get((file_hash, sender), 0))
        self._emit("AccessRequested", requester=sender, granted=granted)
        return granted

    def request_verification(self, sender, file_hash):
        w = self.works.get(file_hash)
        self._emit("VerificationRequested", requester=sender, registered=bool(w))
        return w


if __name__ == "__main__":
    chain = Blockchain("license_chain.json")
    c = IPLicenseContract(chain)
    h = sha256_file("original_work.py")
    fake_now = [time.time()]
    c.now = lambda: fake_now[0]

    print("1. Aman registers the work")
    c.register_work("Aman", h)
    print("2. Aman licenses it to StudioX for 30 days")
    c.grant_access("Aman", h, "StudioX", 30 * 24 * 3600)
    print("3. StudioX requests access (day 1)")
    c.request_access("StudioX", h)
    print("4. Unlicensed user Bob tries to access")
    c.request_access("Bob", h)
    print("5. A court/verifier checks the hash")
    c.request_verification("Court", h)
    print("6. 31 days later, StudioX tries again (license expired)")
    fake_now[0] += 31 * 24 * 3600
    c.request_access("StudioX", h)
    print("7. Bob tries to make Bob the licensee himself")
    try:
        c.grant_access("Bob", h, "Bob", 10**9)
    except PermissionError as e:
        print(f"  rejected: {e}")

    print("\nAudit log of every access attempt:")
    for b in chain.chain:
        if b.data.get("event") == "AccessRequested":
            print(f"  block {b.index}: {b.data['requester']:8} granted={b.data['granted']}")
    print(f"Chain check: {chain.is_valid()[1]}")
