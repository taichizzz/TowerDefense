#include <allegro5/color.h>
#include <allegro5/allegro_primitives.h>
#include <cmath>
#include <utility>

#include "Enemy/Enemy.hpp"
#include "Engine/GameEngine.hpp"
#include "Engine/Group.hpp"
#include "Engine/IObject.hpp"
#include "Engine/IScene.hpp"
#include "Scene/PlayScene.hpp"
#include "Engine/Point.hpp"
#include "Turret.hpp"

PlayScene *Turret::getPlayScene()
{
	return dynamic_cast<PlayScene *>(Engine::GameEngine::GetInstance().GetActiveScene());
}
Turret::Turret(std::string imgBase, std::string imgTurret, float x, float y, float radius, int price, float coolDown) : Sprite(imgTurret, x, y), price(price), coolDown(coolDown), imgBase(imgBase, x, y)
{
	rangeLevel = 0;
	CollisionRadius = radius;
}
void Turret::Update(float deltaTime)
{
	Sprite::Update(deltaTime);
	PlayScene *scene = getPlayScene();
	imgBase.Position = Position;
	imgBase.Tint = Tint;
	if (!Enabled)
		return;
	if (enableTargeting)
	{
		if (Target)
		{
			Engine::Point diff = Target->Position - Position;
			if (diff.Magnitude() > CollisionRadius)
			{
				Target->lockedTurrets.erase(lockedTurretIterator);
				Target = nullptr;
				lockedTurretIterator = std::list<Turret *>::iterator();
			}
		}
		if (!Target)
		{
			// Lock first seen target.
			// Can be improved by Spatial Hash, Quad Tree, ...
			// However simply loop through all enemies is enough for this program.
			for (auto &it : scene->EnemyGroup->GetObjects())
			{
				Engine::Point diff = it->Position - Position;
				if (diff.Magnitude() <= CollisionRadius)
				{
					Target = dynamic_cast<Enemy *>(it);
					Target->lockedTurrets.push_back(this);
					lockedTurretIterator = std::prev(Target->lockedTurrets.end());
					break;
				}
			}
		}
		if (Target)
		{
			Engine::Point originRotation = Engine::Point(cos(Rotation - ALLEGRO_PI / 2), sin(Rotation - ALLEGRO_PI / 2));
			Engine::Point targetRotation = (Target->Position - Position).Normalize();
			float maxRotateRadian = rotateRadian * deltaTime;
			float cosTheta = originRotation.Dot(targetRotation);
			// Might have floating-point precision error.
			if (cosTheta > 1)
				cosTheta = 1;
			else if (cosTheta < -1)
				cosTheta = -1;
			float radian = acos(cosTheta);
			Engine::Point rotation;
			if (abs(radian) <= maxRotateRadian)
				rotation = targetRotation;
			else
				rotation = ((abs(radian) - maxRotateRadian) * originRotation + maxRotateRadian * targetRotation) / radian;
			// Add 90 degrees (PI/2 radian), since we assume the image is oriented upward.
			Rotation = atan2(rotation.y, rotation.x) + ALLEGRO_PI / 2;
			// Shoot reload.
			reload -= deltaTime;
			if (reload <= 0)
			{
				// shoot.
				reload = coolDown;
				CreateBullet();
			}
		}
	}
	else
	{
		// 2. If not targeting, still update reload
		reload -= deltaTime;
		if (reload <= 0)
		{
			reload = coolDown;
			CreateBullet();
		}
	}
}
void Turret::Draw() const
{
	if (Preview)
	{
		al_draw_filled_circle(Position.x, Position.y, CollisionRadius, al_map_rgba(0, 255, 0, 50));
	}
	imgBase.Draw();
	Sprite::Draw();
	if (PlayScene::DebugMode)
	{
		// Draw target radius.
		al_draw_circle(Position.x, Position.y, CollisionRadius, al_map_rgb(0, 0, 255), 2);
	}
}
int Turret::GetPrice() const
{
	return price;
}

int Turret::GetID()
{
	return -1;
}

void Turret::UpgradeRange()
{	
	if (this->rangeLevel < 3)
	{
		CollisionRadius += 100;
		this->rangeLevel++;
		getPlayScene()->AddUpgradeStar(this);
	}
}

void Turret::UpgradeFireRate() {
   if (this->rangeLevel < 3)
	{
		coolDown -= 0.1;
		this->rangeLevel++;
		getPlayScene()->AddUpgradeStar(this);
	}
}