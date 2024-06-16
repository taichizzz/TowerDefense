#include <allegro5/allegro.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <vector>
#include <queue>
#include <string>
#include <memory>

#include "Engine/AudioHelper.hpp"
#include "UI/Animation/DirtyEffect.hpp"
#include "Enemy/Enemy.hpp"
#include "Engine/GameEngine.hpp"
#include "Engine/Group.hpp"
#include "UI/Component/Label.hpp"
#include "Turret/LaserTurret.hpp"
#include "Turret/MachineGunTurret.hpp"
#include "Turret/MissileTurret.hpp"
#include "Turret/Shovel.hpp"
#include "UI/Animation/Plane.hpp"
#include "Enemy/PlaneEnemy.hpp"
#include "PlayScene.hpp"
#include "Engine/Resources.hpp"
#include "Enemy/SoldierEnemy.hpp"
#include "Enemy/TankEnemy.hpp"
#include "Enemy/Tank2Enemy.hpp"
#include "Turret/TurretButton.hpp"
#include "Turret/LightTurret.hpp"
#include "Turret/OctoTurret.hpp"
#include "Turret/RangeBoost.hpp"
#include "Turret/FireRateBoost.hpp"
#include "UI/Component/Pause.hpp"

#define move 160

bool mute = false;
bool PlayScene::DebugMode = false;
const std::vector<Engine::Point> PlayScene::directions = {Engine::Point(-1, 0), Engine::Point(0, -1), Engine::Point(1, 0), Engine::Point(0, 1)};
const int PlayScene::MapWidth = 20, PlayScene::MapHeight = 13;
const int PlayScene::BlockSize = 64;
const float PlayScene::DangerTime = 7.61;
const Engine::Point PlayScene::SpawnGridPoint = Engine::Point(-1, 0);
const Engine::Point PlayScene::EndGridPoint = Engine::Point(MapWidth, MapHeight - 1);
const std::vector<int> PlayScene::code = {ALLEGRO_KEY_UP, ALLEGRO_KEY_UP, ALLEGRO_KEY_DOWN, ALLEGRO_KEY_DOWN,
														ALLEGRO_KEY_LEFT, ALLEGRO_KEY_LEFT, ALLEGRO_KEY_RIGHT, ALLEGRO_KEY_RIGHT,
														ALLEGRO_KEY_B, ALLEGRO_KEY_A, ALLEGRO_KEYMOD_SHIFT, ALLEGRO_KEY_ENTER};

int finalScore;

Engine::Point PlayScene::GetClientSize()
{
	return Engine::Point(MapWidth * BlockSize, MapHeight * BlockSize);
}
void PlayScene::Initialize()
{
	// TODO: [HACKATHON-3-BUG] (1/5): There's a bug in this file, which crashes the game when you lose. Try to find it.
	// TODO: [HACKATHON-3-BUG] (2/5): Find out the cheat code to test.
	// TODO: [HACKATHON-3-BUG] (2/5): It should generate a Plane, and add 10000 to the money, but it doesn't work now.
	mapState.clear();
	oldmapState.clear();
	keyStrokes.clear();
	ticks = 0;
	deathCountDown = -1;
	lives = 10;
	money = 150;
	SpeedMult = 1;
	score = 0;
	isPaused = false;

	// Add groups from bottom to top.
	AddNewObject(TileMapGroup = new Group());
	AddNewObject(GroundEffectGroup = new Group());
	AddNewObject(DebugIndicatorGroup = new Group());
	AddNewObject(TowerGroup = new Group());
	AddNewObject(EnemyGroup = new Group());
	AddNewObject(BulletGroup = new Group());
	AddNewObject(EffectGroup = new Group());
	// Should support buttons.
	AddNewControlObject(UIGroup = new Group());
	AddNewControlObject(UILife = new Group());
	AddNewControlObject(UIStar = new Group());
	ReadMap();
	ReadEnemyWave();
	mapDistance = CalculateBFSDistance();
	ConstructUI();
	imgTarget = new Engine::Image("play/target.png", 0, 0);
	imgTarget->Visible = false;
	preview = nullptr;
	UIGroup->AddNewObject(imgTarget);
	// Preload Lose Scene
	deathBGMInstance = Engine::Resources::GetInstance().GetSampleInstance("astronomia.ogg");
	// Engine::Resources::GetInstance().GetBitmap("lose/benjamin-happy.png");
	//  Start BGM.
	if (!mute)
		bgmId = AudioHelper::PlayBGM("play.ogg");
}
void PlayScene::Terminate()
{
	heartImages.clear();
	AudioHelper::StopBGM(bgmId);
	AudioHelper::StopSample(deathBGMInstance);
	deathBGMInstance = std::shared_ptr<ALLEGRO_SAMPLE_INSTANCE>();
	IScene::Terminate();
}
void PlayScene::Update(float deltaTime)
{
	// If we use deltaTime directly, then we might have Bullet-through-paper problem.
	// Reference: Bullet-Through-Paper
	if (SpeedMult == 0)
		deathCountDown = -1;
	else if (deathCountDown != -1)
		SpeedMult = 1;
	// Calculate danger zone.
	std::vector<float> reachEndTimes;
	for (auto &it : EnemyGroup->GetObjects())
	{
		reachEndTimes.push_back(dynamic_cast<Enemy *>(it)->reachEndTime);
	}
	// Can use Heap / Priority-Queue instead. But since we won't have too many enemies, sorting is fast enough.
	std::sort(reachEndTimes.begin(), reachEndTimes.end());
	float newDeathCountDown = -1;
	int danger = lives;
	for (auto &it : reachEndTimes)
	{
		if (it <= DangerTime)
		{
			danger--;
			if (danger <= 0)
			{
				// Death Countdown
				float pos = DangerTime - it;
				if (it > deathCountDown)
				{
					// Restart Death Count Down BGM.
					AudioHelper::StopSample(deathBGMInstance);
					if (SpeedMult != 0)
						deathBGMInstance = AudioHelper::PlaySample("astronomia.ogg", false, AudioHelper::BGMVolume, pos);
				}
				float alpha = pos / DangerTime;
				alpha = std::max(0, std::min(255, static_cast<int>(alpha * alpha * 255)));
				dangerIndicator->Tint = al_map_rgba(255, 255, 255, alpha);
				newDeathCountDown = it;
				break;
			}
		}
	}
	deathCountDown = newDeathCountDown;
	if (SpeedMult == 0)
		AudioHelper::StopSample(deathBGMInstance);
	if (deathCountDown == -1 && lives > 0)
	{
		AudioHelper::StopSample(deathBGMInstance);
		dangerIndicator->Tint.a = 0;
	}
	if (SpeedMult == 0)
		deathCountDown = -1;
	for (int i = 0; i < SpeedMult; i++)
	{
		IScene::Update(deltaTime);
		// Check if we should create new enemy.
		ticks += deltaTime;
		if (enemyWaveData.empty())
		{
			if (EnemyGroup->GetObjects().empty())
			{
				// Free resources.
				/*delete TileMapGroup;
				delete GroundEffectGroup;
				delete DebugIndicatorGroup;
				delete TowerGroup;
				delete EnemyGroup;
				delete BulletGroup;
				delete EffectGroup;
				delete UIGroup;
				delete imgTarget;*/
				finalScore = lives * 50 + this->score;
				Engine::GameEngine::GetInstance().ChangeScene("win");
			}
			continue;
		}
		auto current = enemyWaveData.front();
		if (ticks < current.second)
			continue;
		ticks -= current.second;
		enemyWaveData.pop_front();
		const Engine::Point SpawnCoordinate = Engine::Point(SpawnGridPoint.x * BlockSize + BlockSize / 2, SpawnGridPoint.y * BlockSize + BlockSize / 2);
		Enemy *enemy;
		switch (current.first)
		{
		case 1:
			EnemyGroup->AddNewObject(enemy = new SoldierEnemy(SpawnCoordinate.x, SpawnCoordinate.y));
			break;
		case 2:
			EnemyGroup->AddNewObject(enemy = new PlaneEnemy(SpawnCoordinate.x, SpawnCoordinate.y));
			break;
		case 3:
			EnemyGroup->AddNewObject(enemy = new TankEnemy(SpawnCoordinate.x, SpawnCoordinate.y));
			// enemy->EnableHeadRotation(true); // Enable head rotation for PlayScene
			break;
		case 4:
			EnemyGroup->AddNewObject(enemy = new Tank2Enemy(SpawnCoordinate.x, SpawnCoordinate.y));
			break;
			// TODO: [CUSTOM-ENEMY]: You need to modify 'Resource/enemy1.txt', or 'Resource/enemy2.txt' to spawn the 4th enemy.
			//         The format is "[EnemyId] [TimeDelay] [Repeat]".
			// TODO: [CUSTOM-ENEMY]: Enable the creation of the enemy.
		default:
			continue;
		}
		enemy->UpdatePath(mapDistance);
		// Compensate the time lost.
		enemy->Update(ticks);
	}
	if (preview)
	{
		preview->Position = Engine::GameEngine::GetInstance().GetMousePosition();
		// To keep responding when paused.
		preview->Update(deltaTime);
	}
}
void PlayScene::Draw() const
{
	IScene::Draw();
	if (DebugMode)
	{
		// Draw reverse BFS distance on all reachable blocks.
		for (int i = 0; i < MapHeight; i++)
		{
			for (int j = 0; j < MapWidth; j++)
			{
				if (mapDistance[i][j] != -1)
				{
					// Not elegant nor efficient, but it's quite enough for debugging.
					Engine::Label label(std::to_string(mapDistance[i][j]), "pirulen.ttf", 32, (j + 0.5) * BlockSize, (i + 0.5) * BlockSize, 255, 255, 255, 255);
					label.Anchor = Engine::Point(0.5, 0.5);
					label.Draw();
				}
			}
		}
	}
}
void PlayScene::OnMouseDown(int button, int mx, int my)
{
	if ((button & 1) && !imgTarget->Visible && preview)
	{
		// Cancel turret construct.
		UIGroup->RemoveObject(preview->GetObjectIterator());
		preview = nullptr;
	}
	IScene::OnMouseDown(button, mx, my);
}
void PlayScene::OnMouseMove(int mx, int my)
{
	IScene::OnMouseMove(mx, my);
	const int x = mx / BlockSize;
	const int y = my / BlockSize;
	if (!preview || x < 0 || x >= MapWidth || y < 0 || y >= MapHeight)
	{
		imgTarget->Visible = false;
		return;
	}
	imgTarget->Visible = true;
	imgTarget->Position.x = x * BlockSize;
	imgTarget->Position.y = y * BlockSize;
}
void PlayScene::OnMouseUp(int button, int mx, int my)
{
	IScene::OnMouseUp(button, mx, my);
	if (!imgTarget->Visible)
		return;
	const int x = mx / BlockSize;
	const int y = my / BlockSize;
	if (button & 1)
	{
		if (mapState[y][x] != TILE_OCCUPIED)
		{
			if (!preview)
				return;
			// Check if valid.
			if (preview->GetID() != 4 && preview->GetID() != 6 && preview->GetID() != 7)
			{
				if (!CheckSpaceValid(x, y))
				{
					Engine::Sprite *sprite;
					GroundEffectGroup->AddNewObject(sprite = new DirtyEffect("play/target-invalid.png", 1, x * BlockSize + BlockSize / 2, y * BlockSize + BlockSize / 2));
					sprite->Rotation = 0;
					return;
				}
				// Purchase.
				EarnMoney(-preview->GetPrice());
				// Remove Preview.
				preview->GetObjectIterator()->first = false;
				UIGroup->RemoveObject(preview->GetObjectIterator());
				// Construct real turret.
				preview->Position.x = x * BlockSize + BlockSize / 2;
				preview->Position.y = y * BlockSize + BlockSize / 2;
				preview->Enabled = true;
				preview->Preview = false;
				preview->Tint = al_map_rgba(255, 255, 255, 255);
				TowerGroup->AddNewObject(preview);
				// To keep responding when paused.
				preview->Update(0);
				// Remove Preview.
				preview = nullptr;

				mapState[y][x] = TILE_OCCUPIED;
				OnMouseMove(mx, my);
			}
			else
			{
				if ((preview->GetID() == 4) && (mapState[y][x] == TILE_FLOOR || mapState[y][x] == TILE_DIRT))
				{
					Engine::Sprite *sprite;
					GroundEffectGroup->AddNewObject(sprite = new DirtyEffect("play/target-invalid.png", 1, x * BlockSize + BlockSize / 2, y * BlockSize + BlockSize / 2));
					sprite->Rotation = 0;
					return;
				}
				else if ((preview->GetID() == 6) && (mapState[y][x] == TILE_FLOOR || mapState[y][x] == TILE_DIRT))
				{
					Engine::Sprite *sprite;
					GroundEffectGroup->AddNewObject(sprite = new DirtyEffect("play/target-invalid.png", 1, x * BlockSize + BlockSize / 2, y * BlockSize + BlockSize / 2));
					sprite->Rotation = 0;
					return;
				}
				else if ((preview->GetID() == 7) && (mapState[y][x] == TILE_FLOOR || mapState[y][x] == TILE_DIRT))
				{
					Engine::Sprite *sprite;
					GroundEffectGroup->AddNewObject(sprite = new DirtyEffect("play/target-invalid.png", 1, x * BlockSize + BlockSize / 2, y * BlockSize + BlockSize / 2));
					sprite->Rotation = 0;
					return;
				}
				auto towers = TowerGroup->GetObjects();
				Engine::Point pnt;
				pnt.x = x * BlockSize + BlockSize / 2;
				pnt.y = y * BlockSize + BlockSize / 2;
			}
		}
		if (preview && preview->GetID() == 4)
		{ // Check if a Shovel is selected
			const int x = mx / BlockSize;
			const int y = my / BlockSize;

			auto towers = TowerGroup->GetObjects();
			Engine::Point pnt;
			pnt.x = x * BlockSize + BlockSize / 2;
			pnt.y = y * BlockSize + BlockSize / 2;

			for (auto &it : towers)
			{
				IObject *tower = dynamic_cast<IObject *>(it);
				if (tower->Position.x == pnt.x && tower->Position.y == pnt.y)
				{
					Turret *turretToRemove = dynamic_cast<Turret *>(tower);
					// Remove stars
					for (Engine::Image *star : turretStars[turretToRemove])
					{
						// Check if the star is still in the UIGroup
						auto starIt = std::find(UIStar->GetObjects().begin(), UIStar->GetObjects().end(), star);
						if (starIt != UIStar->GetObjects().end())
						{
							UIStar->RemoveObject(star->GetObjectIterator());
						}
					}
					turretStars.erase(turretToRemove);

					// Remove the turret
					EarnMoney(dynamic_cast<Turret *>(tower)->GetPrice() / 2 + dynamic_cast<Turret *>(tower)->rangeLevel * dynamic_cast<Turret *>(tower)->GetPrice() / 4);
					TowerGroup->RemoveObject(tower->GetObjectIterator());
					mapState[y][x] = oldmapState[y][x];

					// preview->GetObjectIterator()->first = false;
					UIGroup->RemoveObject(preview->GetObjectIterator());

					// preview->Update(0);
					preview = nullptr;
					// OnMouseMove(mx, my);
					mapDistance = CalculateBFSDistance();
					return;
				}
			}
		}
		else if (preview && preview->GetID() == 6) // RangeBoost
		{
			const int x = mx / BlockSize;
			const int y = my / BlockSize;

			auto towers = TowerGroup->GetObjects();
			Engine::Point pnt;
			pnt.x = x * BlockSize + BlockSize / 2;
			pnt.y = y * BlockSize + BlockSize / 2;

			for (auto &it : towers)
			{
				IObject *tower = dynamic_cast<IObject *>(it);
				if (tower->Position.x == pnt.x && tower->Position.y == pnt.y)
				{
					if (dynamic_cast<RangeBoost *>(preview))
					{
						Turret *clickedTurret = dynamic_cast<Turret *>(tower);

						if (clickedTurret && clickedTurret->rangeLevel < 2)
						{
							if (clickedTurret->GetID() != 2 && clickedTurret->GetID() != 5)
							{
								int rangeBoostPrice = clickedTurret->GetPrice() / 2;
								if (money >= rangeBoostPrice)
								{
									clickedTurret->UpgradeRange();
									EarnMoney(-rangeBoostPrice);
								}
							}
							else
								return;
						}
						else
						{
							return;
						}

						UIGroup->RemoveObject(preview->GetObjectIterator());
						preview = nullptr;
						return;
					}
				}
			}
		}
		else if (preview && preview->GetID() == 7) // RangeBoost
		{
			const int x = mx / BlockSize;
			const int y = my / BlockSize;

			auto towers = TowerGroup->GetObjects();
			Engine::Point pnt;
			pnt.x = x * BlockSize + BlockSize / 2;
			pnt.y = y * BlockSize + BlockSize / 2;

			for (auto &it : towers)
			{
				IObject *tower = dynamic_cast<IObject *>(it);
				if (tower->Position.x == pnt.x && tower->Position.y == pnt.y)
				{
					if (dynamic_cast<FireRateBoost *>(preview))
					{
						Turret *clickedTurret = dynamic_cast<Turret *>(tower);

						if (clickedTurret && clickedTurret->rangeLevel < 2)
						{

							int FireRateBoostPrice = clickedTurret->GetPrice() / 2;
							if (money >= FireRateBoostPrice)
							{
								clickedTurret->UpgradeFireRate();
								EarnMoney(-FireRateBoostPrice);
							}
						}
						else
						{
							return;
						}

						UIGroup->RemoveObject(preview->GetObjectIterator());
						preview = nullptr;
						return;
					}
				}
			}
		}
	}
}

void PlayScene::OnKeyDown(int keyCode)
{
	IScene::OnKeyDown(keyCode);
	if (keyCode == ALLEGRO_KEY_TAB)
	{
		DebugMode = !DebugMode;
	}
	else
	{
		keyStrokes.push_back(keyCode);
		if (keyStrokes.size() > code.size())
			keyStrokes.pop_front();
		if (keyCode == ALLEGRO_KEY_ENTER && keyStrokes.size() == code.size())
		{
			auto it = keyStrokes.begin();
			for (int c : code)
			{
				if (!((*it == c) ||
						(c == ALLEGRO_KEYMOD_SHIFT &&
						 (*it == ALLEGRO_KEY_LSHIFT || *it == ALLEGRO_KEY_RSHIFT))))
					return;
				++it;
			}
			EffectGroup->AddNewObject(new Plane());
			EarnMoney(10000);
		}
	}
	if (keyCode == ALLEGRO_KEY_Q)
	{
		// Hotkey for MachineGunTurret.
		UIBtnClicked(0);
	}
	else if (keyCode == ALLEGRO_KEY_W)
	{
		// Hotkey for LaserTurret.
		UIBtnClicked(1);
	}
	else if (keyCode == ALLEGRO_KEY_E)
	{
		// Hotkey for MissileTurret.
		UIBtnClicked(2);
	}
	else if (keyCode == ALLEGRO_KEY_R)
	{
		// Hotkey for LightTurret.
		UIBtnClicked(3);
	}
	else if (keyCode == ALLEGRO_KEY_T)
	{
		// Hotkey for Shovel.
		UIBtnClicked(4);
	}
	else if (keyCode == ALLEGRO_KEY_Y)
	{
		// Hotkey for OctaTurret.
		UIBtnClicked(5);
	}
	else if (keyCode == ALLEGRO_KEY_U)
	{
		// Hotkey for RangeBoost.
		UIBtnClicked(6);
	}
	else if (keyCode == ALLEGRO_KEY_I)
	{
		// Hotkey for FireRateBoost.
		UIBtnClicked(7);
	}

	else if (keyCode == ALLEGRO_KEY_M)
	{
		// Hotkey for muting cuz it's loud when debugging xD
		mute = !mute;
		if (mute)
			AudioHelper::StopBGM(bgmId);
	}

	// TODO: [CUSTOM-TURRET]: Make specific key to create the turret.
	else if (keyCode >= ALLEGRO_KEY_0 && keyCode <= ALLEGRO_KEY_9)
	{
		// Hotkey for Speed up.
		SpeedMult = keyCode - ALLEGRO_KEY_0;
	}
}
void PlayScene::Hit()
{
	lives--;
	// UILives->Text = std::string("Life ") + std::to_string(lives);

	Engine::ImageButton *lastHeart = heartImages.back();
	UILife->RemoveObject(lastHeart->GetObjectIterator());
	heartImages.pop_back();

	if (lives <= 0)
	{
		Engine::GameEngine::GetInstance().ChangeScene("lose");
	}
}

void PlayScene::Score(int scores)
{
	this->score += scores;
	UIScore->Text = std::string("Score ") + std::to_string(this->score);
}

int PlayScene::GetMoney() const
{
	return money;
}
void PlayScene::EarnMoney(int money)
{
	this->money += money;
	UIMoney->Text = std::string("$ ") + std::to_string(this->money);
}
void PlayScene::ReadMap()
{
	std::string filename = std::string("Resource/map") + std::to_string(MapId) + ".txt";
	// Read map file.
	char c;
	std::vector<bool> mapData;
	std::ifstream fin(filename);
	while (fin >> c)
	{
		switch (c)
		{
		case '0':
			mapData.push_back(false);
			break;
		case '1':
			mapData.push_back(true);
			break;
		case '\n':
		case '\r':
			if (static_cast<int>(mapData.size()) / MapWidth != 0)
				throw std::ios_base::failure("Map data is corrupted.");
			break;
		default:
			throw std::ios_base::failure("Map data is corrupted.");
		}
	}
	fin.close();
	// Validate map data.
	if (static_cast<int>(mapData.size()) != MapWidth * MapHeight)
		throw std::ios_base::failure("Map data is corrupted.");
	// Store map in 2d array.
	mapState = std::vector<std::vector<TileType>>(MapHeight, std::vector<TileType>(MapWidth));
	oldmapState = std::vector<std::vector<TileType>>(MapHeight, std::vector<TileType>(MapWidth));
	for (int i = 0; i < MapHeight; i++)
	{
		for (int j = 0; j < MapWidth; j++)
		{
			const int num = mapData[i * MapWidth + j];
			mapState[i][j] = oldmapState[i][j] = num ? TILE_FLOOR : TILE_DIRT;
			if (num)
				TileMapGroup->AddNewObject(new Engine::Image("play/floor3.png", j * BlockSize, i * BlockSize, BlockSize, BlockSize));
			else
				TileMapGroup->AddNewObject(new Engine::Image("play/dirt2.png", j * BlockSize, i * BlockSize, BlockSize, BlockSize));
		}
	}
}
void PlayScene::ReadEnemyWave()
{
	// TODO: [HACKATHON-3-BUG] (3/5): Trace the code to know how the enemies are created.
	// TODO: [HACKATHON-3-BUG] (3/5): There is a bug in these files, which let the game only spawn the first enemy, try to fix it.
	std::string filename = std::string("Resource/enemy") + std::to_string(MapId) + ".txt";
	// Read enemy file.
	float type, wait, repeat;
	enemyWaveData.clear();
	std::ifstream fin(filename);
	while (fin >> type && fin >> wait && fin >> repeat)
	{
		for (int i = 0; i < repeat; i++)
			enemyWaveData.emplace_back(type, wait);
	}
	fin.close();
}
void PlayScene::ConstructUI()
{
	// Background
	UIGroup->AddNewObject(new Engine::Image("play/sidebar3.png", 1280, 0, 320, 832));
	// Text
	UIGroup->AddNewObject(new Engine::Label(std::string("Stage ") + std::to_string(MapId), "pirulen.ttf", 32, 1294 + 60, 12));
	UIGroup->AddNewObject(UIMoney = new Engine::Label(std::string("$ ") + std::to_string(money), "pirulen.ttf", 48, 1294 + 66, 60));
	// UIGroup->AddNewObject(UILives = new Engine::Label(std::string("Life ") + std::to_string(lives), "pirulen.ttf", 24, 1294, 88));
	UIGroup->AddNewObject(UIScore = new Engine::Label(std::string("Score ") + std::to_string(score), "pirulen.ttf", 26, 1294 + 48, 775 - 50));
	UIGroup->AddNewObject(new Engine::Label(std::string("Turrets"), "pirulen.ttf", 26, 1294 + 54, 240));
	UIGroup->AddNewObject(new Engine::Label(std::string("Tools"), "pirulen.ttf", 26, 1294 + 72, 460));
	UIGroup->AddNewObject(new Engine::Label(std::string("PowerUps"), "pirulen.ttf", 26, 1294 + 40, 590));
	TurretButton *btn;

	Engine::ImageButton *lifebtn;
	int five = 1;
	for (int i = 0; i < lives; i++)
	{
		if (five <= 5)
		{
			lifebtn = new Engine::ImageButton("play/heart.png", "play/heart.png", 1294 + i * 48 + 18, 124, 56, 56);
		}
		else
			lifebtn = new Engine::ImageButton("play/heart.png", "play/heart.png", 1294 + (i) * 48 - (5 * 48) + 18, 124 + 39, 56, 56);
		five++;
		UILife->AddNewControlObject(lifebtn);
		heartImages.push_back(lifebtn);
	}

	// Button 1
	btn = new TurretButton("play/floor.png", "play/dirt.png",
								  Engine::Sprite("play/tower-base.png", 1294, 136 + move, 0, 0, 0, 0),
								  Engine::Sprite("play/turret-1.png", 1294, 136 - 8 + move, 0, 0, 0, 0), 1294, 136 + move, MachineGunTurret::Price);
	// Reference: Class Member Function Pointer and std::bind.
	btn->SetOnClickCallback(std::bind(&PlayScene::UIBtnClicked, this, 0));
	UIGroup->AddNewControlObject(btn);
	// Button 2
	btn = new TurretButton("play/floor.png", "play/dirt.png",
								  Engine::Sprite("play/tower-base.png", 1370, 136 + move, 0, 0, 0, 0),
								  Engine::Sprite("play/turret-2.png", 1370, 136 - 8 + move, 0, 0, 0, 0), 1370, 136 + move, LaserTurret::Price);
	btn->SetOnClickCallback(std::bind(&PlayScene::UIBtnClicked, this, 1));
	UIGroup->AddNewControlObject(btn);
	// Button 3
	btn = new TurretButton("play/floor.png", "play/dirt.png",
								  Engine::Sprite("play/tower-base.png", 1446, 136 + move, 0, 0, 0, 0),
								  Engine::Sprite("play/turret-3.png", 1446, 136 + move, 0, 0, 0, 0), 1446, 136 + move, MissileTurret::Price);
	btn->SetOnClickCallback(std::bind(&PlayScene::UIBtnClicked, this, 2));
	UIGroup->AddNewControlObject(btn);
	// TODO: [CUSTOM-TURRET]: Create a button to support constructing the turret.

	btn = new TurretButton("play/floor.png", "play/dirt.png",
								  Engine::Sprite("play/tower-base.png", 1522, 136 + move, 0, 0, 0, 0),
								  Engine::Sprite("play/turret-7.png", 1522, 136 + move, 0, 0, 0, 0), 1522, 136 + move, LightTurret::Price);
	btn->SetOnClickCallback(std::bind(&PlayScene::UIBtnClicked, this, 3));
	UIGroup->AddNewControlObject(btn);

	btn = new TurretButton("play/floor.png", "play/dirt.png",
								  Engine::Sprite("play/tower-base.png", 1294, 212 + 2 * move - 30, 0, 0, 0, 0),
								  Engine::Sprite("play/shovel1.png", 1294, 212 + 2 * move - 30, 0, 0, 0, 0), 1294, 212 + 2 * move - 30, Shovel::Price);
	btn->SetOnClickCallback(std::bind(&PlayScene::UIBtnClicked, this, 4));
	UIGroup->AddNewControlObject(btn);

	btn = new TurretButton("play/floor.png", "play/dirt.png",
								  Engine::Sprite("play/tower-base.png", 1294, 212 + move, 0, 0, 0, 0),
								  Engine::Sprite("play/turret-octa.png", 1294, 212 + move, 0, 0, 0, 0), 1294, 212 + move, OctoTurret::Price);
	btn->SetOnClickCallback(std::bind(&PlayScene::UIBtnClicked, this, 5));
	UIGroup->AddNewControlObject(btn);

	btn = new TurretButton("play/floor.png", "play/dirt.png",
								  Engine::Sprite("play/tower-base.png", 1294, 212 + 3 * move - 60, 0, 0, 0, 0),
								  Engine::Sprite("play/rangeboost.png", 1294, 212 + 3 * move - 60, 0, 0, 0, 0), 1294, 212 + 3 * move - 60, 25);
	btn->SetOnClickCallback(std::bind(&PlayScene::UIBtnClicked, this, 6));
	UIGroup->AddNewControlObject(btn);

	btn = new TurretButton("play/floor.png", "play/dirt.png",
								  Engine::Sprite("play/tower-base.png", 1370, 212 + 3 * move - 60, 0, 0, 0, 0),
								  Engine::Sprite("play/firerateboost.png", 1370, 212 + 3 * move - 60, 0, 0, 0, 0), 1370, 212 + 3 * move - 60, 25);
	btn->SetOnClickCallback(std::bind(&PlayScene::UIBtnClicked, this, 7));
	UIGroup->AddNewControlObject(btn);

	Engine::PauseButton *pausebtn;

	int w = Engine::GameEngine::GetInstance().GetScreenSize().x;
	int h = Engine::GameEngine::GetInstance().GetScreenSize().y;
	int halfW = w / 2;
	int halfH = h / 2;
	pausebtn = new Engine::PauseButton("play/play2.png", "play/play5.png", halfW + 500 + 62.5, halfH * 7 / 4 + 40, 125, 50.625);
	//pausebtn->SetOnClickCallback(std::bind(&PlayScene::PauseGame()));
	AddNewControlObject(pausebtn);
	AddNewObject(new Engine::Label("Pause", "pirulen.ttf", 16, halfW + 625, halfH * 7 / 4 + 65, 0, 0, 0, 255, 0.5, 0.5));

	//int w = Engine::GameEngine::GetInstance().GetScreenSize().x;
	//int h = Engine::GameEngine::GetInstance().GetScreenSize().y;
	int shift = 135 + 25;
	dangerIndicator = new Engine::Sprite("play/benjamin.png", w - shift, h - shift);
	dangerIndicator->Tint.a = 0;
	UIGroup->AddNewObject(dangerIndicator);
}

void PlayScene::UIBtnClicked(int id)
{
	if (preview)
		UIGroup->RemoveObject(preview->GetObjectIterator());
	// TODO: [CUSTOM-TURRET]: On callback, create the turret.
	if (id == 0 && money >= MachineGunTurret::Price)
		preview = new MachineGunTurret(0, 0);
	else if (id == 1 && money >= LaserTurret::Price)
		preview = new LaserTurret(0, 0);
	else if (id == 2 && money >= MissileTurret::Price)
		preview = new MissileTurret(0, 0);
	else if (id == 3 && money >= LightTurret::Price)
		preview = new LightTurret(0, 0);
	else if (id == 4 && money >= Shovel::Price)
		preview = new Shovel(0, 0);
	else if (id == 5 && money >= OctoTurret::Price)
		preview = new OctoTurret(0, 0);
	else if (id == 6 && money >= RangeBoost::Price)
		preview = new RangeBoost(0, 0);
	else if (id == 7 && money >= FireRateBoost::Price)
		preview = new FireRateBoost(0, 0);
	if (!preview)
		return;
	preview->Position = Engine::GameEngine::GetInstance().GetMousePosition();
	preview->Tint = al_map_rgba(255, 255, 255, 200);
	preview->Enabled = false;
	preview->Preview = true;
	UIGroup->AddNewObject(preview);
	OnMouseMove(Engine::GameEngine::GetInstance().GetMousePosition().x, Engine::GameEngine::GetInstance().GetMousePosition().y);
}

bool PlayScene::CheckSpaceValid(int x, int y)
{
	if (x < 0 || x >= MapWidth || y < 0 || y >= MapHeight)
		return false;
	auto map00 = mapState[y][x];
	mapState[y][x] = TILE_OCCUPIED;
	std::vector<std::vector<int>> map = CalculateBFSDistance();
	mapState[y][x] = map00;
	if (map[0][0] == -1)
		return false;
	for (auto &it : EnemyGroup->GetObjects())
	{
		Engine::Point pnt;
		pnt.x = floor(it->Position.x / BlockSize);
		pnt.y = floor(it->Position.y / BlockSize);
		if (pnt.x < 0)
			pnt.x = 0;
		if (pnt.x >= MapWidth)
			pnt.x = MapWidth - 1;
		if (pnt.y < 0)
			pnt.y = 0;
		if (pnt.y >= MapHeight)
			pnt.y = MapHeight - 1;
		if (map[pnt.y][pnt.x] == -1)
			return false;
	}
	// All enemy have path to exit.
	mapState[y][x] = TILE_OCCUPIED;
	mapDistance = map;
	for (auto &it : EnemyGroup->GetObjects())
		dynamic_cast<Enemy *>(it)->UpdatePath(mapDistance);
	return true;
}

std::vector<std::vector<int>> PlayScene::CalculateBFSDistance()
{
	// Reverse BFS to find path.
	std::vector<std::vector<int>> map(MapHeight, std::vector<int>(MapWidth, -1));
	std::queue<Engine::Point> que;
	// Push end point.
	// BFS from end point.
	if (mapState[MapHeight - 1][MapWidth - 1] != TILE_DIRT)
		return map;

	que.push(Engine::Point(MapWidth - 1, MapHeight - 1));
	map[MapHeight - 1][MapWidth - 1] = 0;
	while (!que.empty())
	{
		Engine::Point p = que.front();
		que.pop();

		int x = p.x;
		int y = p.y;

		// Explore adjacent cells (up, down, left, right)
		// If the cell is empty and unvisited, mark its distance and add it to the queue.
		// Up
		if (y > 0 && mapState[y - 1][x] == TILE_DIRT && map[y - 1][x] == -1)
		{
			map[y - 1][x] = map[y][x] + 1;
			que.push(Engine::Point(x, y - 1));
		}
		// Down
		if (y < MapHeight - 1 && mapState[y + 1][x] == TILE_DIRT && map[y + 1][x] == -1)
		{
			map[y + 1][x] = map[y][x] + 1;
			que.push(Engine::Point(x, y + 1));
		}
		// Left
		if (x > 0 && mapState[y][x - 1] == TILE_DIRT && map[y][x - 1] == -1)
		{
			map[y][x - 1] = map[y][x] + 1;
			que.push(Engine::Point(x - 1, y));
		}
		// Right
		if (x < MapWidth - 1 && mapState[y][x + 1] == TILE_DIRT && map[y][x + 1] == -1)
		{
			map[y][x + 1] = map[y][x] + 1;
			que.push(Engine::Point(x + 1, y));
		}
	}
	return map;
}

void PlayScene::AddUpgradeStar(Turret *turret)
{
	int starX = turret->Position.x - 32;
	int starY = turret->Position.y - 32;
	starX += 16 * (turret->rangeLevel - 1);
	Engine::Image *starImage = new Engine::Image("play/star.png", starX, starY, 32, 32);
	UIStar->AddNewObject(starImage);
	turretStars[turret].push_back(starImage);
}

void PlayScene::PauseGame() {
    isPaused = !isPaused;

    if (isPaused) {
        // 1. Store the current SpeedMult 
        previousSpeedMult = SpeedMult;
        
        // 2. Set SpeedMult to 0 to pause the game logic
        SpeedMult = 0;

        //AudioHelper::PauseBGM(bgmId); // Optional: Pause music
    } else {
        // 3. Restore the previous SpeedMult value 
        SpeedMult = previousSpeedMult; 

        //AudioHelper::ResumeBGM(bgmId); // Optional: Resume music
    }
}