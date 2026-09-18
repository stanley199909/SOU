#ifndef __PLAYER_H__
#define __PLAYER_H__

#include "DirectXTex/SimpleMath.h"
#include "Box.h"
#include <vector>

// 鍛冶場を歩き回るプレイヤ(一人称/相機追随を想定)。
//  役割:
//   ・位置と向き(yaw)・移動速度を持つ「物件」= 理由2,3
//   ・壁(Box)に当たって止まる (MoveWithWall)
//   ・「可互動」状態のとき、互動キーで工程状態への遷移を要求できる = 理由1
//     (どの道具か・どの状態へ行くかの判定は SceneForge 側。Playerは状態と入力だけ持つ)
//   ・他物件は GetPosition()/GetBox() で Player を参照して判定できる = 理由4
class Player
{
public:
	Player();

	// pos:初期座標 / yaw:初期の向き(ラジアン)
	void Init(DirectX::XMFLOAT3 pos, float yaw = 0.0f);
	// tick:デルタタイム / walls:当たり判定を取る壁の配列
	void Update(float tick, std::vector<Box>& walls);

	//--- 参照(相機・他物件から読む) = 理由4 ---
	DirectX::XMFLOAT3 GetPosition() const;	// 現在座標
	float             GetYaw() const;		// 向き(Y軸回転ラジアン)
	DirectX::XMFLOAT3 GetForward() const;	// 向きの前方ベクトル(相機/互動判定に使う)
	Box&              GetBox();				// 当たり判定用ボックス

	//--- 向き(相機/マウスルックが設定。Step2で接続) ---
	void SetYaw(float yaw) { m_yaw = yaw; }

	//--- 互動 = 理由1 ---
	// シーンが毎フレーム設定する: 近くに「使える道具」があるか
	void SetCanInteract(bool v) { m_canInteract = v; }
	bool CanInteract() const { return m_canInteract; }
	// 可互動 かつ 互動キーが押された瞬間 なら true (SceneForgeが状態遷移に使う)
	bool WantInteract() const;

	//--- 調整値 = 理由3 (将来ここに値を足していく) ---
	void  SetMoveSpeed(float s) { m_moveSpeed = s; }
	float GetMoveSpeed() const { return m_moveSpeed; }

private:
	// 指定した移動量を、壁と衝突しない範囲だけ適用する(軸ごとに処理)
	void MoveWithWall(DirectX::XMFLOAT3 vel, std::vector<Box>& walls);

private:
	Box   m_box;					// 当たり判定 & 座標保持
	float m_yaw = 0.0f;				// 向き(ラジアン)
	float m_moveSpeed = 3.0f;		// 移動速度(単位/秒) = 理由3
	bool  m_canInteract = false;	// 近くに使える道具があるか(シーンが設定) = 理由1
};

#endif // __PLAYER_H__
