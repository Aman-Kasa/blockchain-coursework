// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/// @title IPLicense - register file hashes and grant time-limited, per-user access
/// @notice Deploy in Remix (remix.ethereum.org) or on Sepolia testnet.
contract IPLicense {
    struct Work {
        address owner;
        uint256 registeredAt;
        bool exists;
    }

    mapping(bytes32 => Work) public works;
    // fileHash => licensee => access expiry (unix time)
    mapping(bytes32 => mapping(address => uint256)) public accessUntil;

    event WorkRegistered(bytes32 indexed fileHash, address indexed owner, uint256 timestamp);
    event AccessGranted(bytes32 indexed fileHash, address indexed user, uint256 expiresAt);
    event AccessRevoked(bytes32 indexed fileHash, address indexed user);
    // Every request is logged on-chain, granted or not -> audit trail of who asked, when
    event AccessRequested(bytes32 indexed fileHash, address indexed requester, uint256 timestamp, bool granted);
    event VerificationRequested(bytes32 indexed fileHash, address indexed requester, uint256 timestamp, bool registered);

    modifier onlyOwner(bytes32 fileHash) {
        require(works[fileHash].owner == msg.sender, "Not the owner");
        _;
    }

    function registerWork(bytes32 fileHash) external {
        require(!works[fileHash].exists, "Already registered");
        works[fileHash] = Work(msg.sender, block.timestamp, true);
        emit WorkRegistered(fileHash, msg.sender, block.timestamp);
    }

    /// @param duration seconds of access, e.g. 30 days = 2592000
    function grantAccess(bytes32 fileHash, address user, uint256 duration) external onlyOwner(fileHash) {
        uint256 expiry = block.timestamp + duration;
        accessUntil[fileHash][user] = expiry;
        emit AccessGranted(fileHash, user, expiry);
    }

    function revokeAccess(bytes32 fileHash, address user) external onlyOwner(fileHash) {
        accessUntil[fileHash][user] = 0;
        emit AccessRevoked(fileHash, user);
    }

    function requestAccess(bytes32 fileHash) external returns (bool granted) {
        require(works[fileHash].exists, "Unknown work");
        granted = msg.sender == works[fileHash].owner
            || block.timestamp <= accessUntil[fileHash][msg.sender];
        emit AccessRequested(fileHash, msg.sender, block.timestamp, granted);
    }

    function requestVerification(bytes32 fileHash) external returns (bool registered, address owner, uint256 registeredAt) {
        Work memory w = works[fileHash];
        emit VerificationRequested(fileHash, msg.sender, block.timestamp, w.exists);
        return (w.exists, w.owner, w.registeredAt);
    }
}
